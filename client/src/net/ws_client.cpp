#include "ws_client.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─── lws static callback shim ─────────────────────────────────────────────────

static int lws_callback_shim(lws* wsi, lws_callback_reasons reason,
                             void* /*user*/, void* in, size_t len) {
    // Retrieve the WsClient pointer stored as protocol user data
    lws_context* ctx = lws_get_context(wsi);
    WsClient* client = static_cast<WsClient*>(lws_context_user(ctx));
    if (!client) return 0;
    return client->on_lws_event(wsi, reason, in, len);
}

// ─── Protocol definitions ─────────────────────────────────────────────────────

static lws_protocols g_protocols[] = {
    {
        "norichat",          // must match server protocol name
        lws_callback_shim,
        0,                   // per-session data size
        65536,               // rx_buffer_size
        0, nullptr, 0
    },
    { nullptr, nullptr, 0, 0 }
};

// ─── WsClient ─────────────────────────────────────────────────────────────────

WsClient::WsClient()  = default;
WsClient::~WsClient() { disconnect(); }

bool WsClient::connect(const std::string& host, int port,
                       const std::string& token) {
    token_   = token;
    running_ = true;

    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = CONTEXT_PORT_NO_LISTEN; // client mode
    info.protocols = g_protocols;
    info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; // harmless without SSL
    info.user      = this; // stored as context user data

    lws_set_log_level(LLL_ERR, nullptr); // minimal noise

    ctx_ = lws_create_context(&info);
    if (!ctx_) {
        fprintf(stderr, "[ws_client] failed to create lws context\n");
        running_ = false;
        return false;
    }

    // Initiate connection
    lws_client_connect_info cci;
    memset(&cci, 0, sizeof(cci));
    cci.context        = ctx_;
    cci.address        = host.c_str();
    cci.port           = port;
    cci.path           = "/ws";
    cci.host           = host.c_str();
    cci.origin         = host.c_str();
    cci.protocol       = g_protocols[0].name;
    cci.ssl_connection = 0; // plain ws://

    wsi_ = lws_client_connect_via_info(&cci);
    if (!wsi_) {
        fprintf(stderr, "[ws_client] lws_client_connect_via_info failed\n");
        lws_context_destroy(ctx_);
        ctx_     = nullptr;
        running_ = false;
        return false;
    }

    // Start background service thread
    service_thread_ = std::thread(&WsClient::service_thread_fn, this);
    return true;
}

void WsClient::disconnect() {
    running_ = false;
    if (service_thread_.joinable())
        service_thread_.join();
    if (ctx_) {
        lws_context_destroy(ctx_);
        ctx_ = nullptr;
    }
    wsi_       = nullptr;
    connected_ = false;
}

void WsClient::send(const std::string& json_msg) {
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_queue_.push_back(json_msg);
    }
    // lws_cancel_service is the only thread-safe way to wake the service loop
    // from a thread that isn't the one running lws_service().
    if (ctx_) lws_cancel_service(ctx_);
}

// ─── Service loop (background thread) ────────────────────────────────────────

void WsClient::service_thread_fn() {
    while (running_) {
        // Request writable callback from within the service thread (thread-safe).
        {
            std::lock_guard<std::mutex> lock(send_mutex_);
            if (!send_queue_.empty() && wsi_ && connected_)
                lws_callback_on_writable(wsi_);
        }
        lws_service(ctx_, 50); // 50 ms timeout
    }
}

// ─── lws event handler ────────────────────────────────────────────────────────

int WsClient::on_lws_event(lws* wsi, lws_callback_reasons reason,
                           void* in, size_t len) {
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        connected_ = true;
        fprintf(stdout, "[ws_client] connected to server\n");
        // First thing: authenticate
        {
            json auth_msg;
            auth_msg["op"]    = "AUTH";
            auth_msg["token"] = token_;
            send(auth_msg.dump());
        }
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        recv_buf_.append(static_cast<char*>(in), len);
        if (!lws_is_final_fragment(wsi)) break;

        std::string complete = std::move(recv_buf_);
        recv_buf_.clear();

        if (on_message_) on_message_(complete);
        break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (send_queue_.empty()) break;

        const std::string& msg = send_queue_.front();
        size_t msg_len = msg.size();

        std::vector<unsigned char> buf(LWS_PRE + msg_len);
        memcpy(buf.data() + LWS_PRE, msg.data(), msg_len);

        lws_write(wsi, buf.data() + LWS_PRE, msg_len, LWS_WRITE_TEXT);
        send_queue_.pop_front();

        if (!send_queue_.empty())
            lws_callback_on_writable(wsi);
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        fprintf(stderr, "[ws_client] connection error: %s\n",
                in ? (char*)in : "(none)");
        connected_ = false;
        running_   = false;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        connected_ = false;
        running_   = false;
        fprintf(stdout, "[ws_client] connection closed\n");
        break;

    default:
        break;
    }
    return 0;
}
