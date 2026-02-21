#pragma once
#include <libwebsockets.h>
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <functional>

// Asynchronous WebSocket client.
// The lws service loop runs in a background thread.
// Received messages are passed to on_message callback (called from bg thread).
// send() is thread-safe.

class WsClient {
public:
    using MessageCallback = std::function<void(const std::string& json)>;

    WsClient();
    ~WsClient();

    // Connect to ws://host:port/ws and send AUTH with token.
    // Returns true if the context was created successfully.
    bool connect(const std::string& host, int port, const std::string& token);

    // Disconnect and stop the service thread.
    void disconnect();

    // Enqueue a JSON message to be sent (thread-safe).
    void send(const std::string& json_msg);

    bool is_connected() const { return connected_; }

    // Called from the bg thread when a complete message arrives.
    void set_on_message(MessageCallback cb) { on_message_ = std::move(cb); }

    // lws callback – public so the static C shim can access it.
    int on_lws_event(lws* wsi, lws_callback_reasons reason,
                     void* in, size_t len);

private:
    void service_thread_fn();

    lws_context*    ctx_        = nullptr;
    lws*            wsi_        = nullptr;
    bool            connected_  = false;
    volatile bool   running_    = false;
    std::string     token_;

    std::mutex              send_mutex_;
    std::deque<std::string> send_queue_;
    std::string             recv_buf_;   // accumulate fragments

    std::thread      service_thread_;
    MessageCallback  on_message_;
};
