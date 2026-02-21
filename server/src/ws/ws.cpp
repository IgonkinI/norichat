#include "ws.h"
#include "../auth/auth.h"
#include "../db/db.h"
#include "../../../shared/protocol/messages.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <ctime>

using json = nlohmann::json;

// ─── Global session registry ──────────────────────────────────────────────────

static std::map<lws*, ws::Session> g_sessions;

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Enqueue a message for delivery and request a WRITEABLE callback.
static void enqueue(lws* wsi, const std::string& msg) {
    g_sessions[wsi].write_queue.push_back(msg);
    lws_callback_on_writable(wsi);
}

// Send error JSON to client.
static void send_error(lws* wsi, const std::string& op, const std::string& msg) {
    json j;
    j["op"]    = op;
    j["error"] = msg;
    enqueue(wsi, j.dump());
}

// ─── Message handlers ─────────────────────────────────────────────────────────

static void handle_auth(lws* wsi, ws::Session& session, const json& msg) {
    std::string token = msg.value("token", "");
    auto uid = auth::validate_jwt(token);
    if (!uid) {
        send_error(wsi, OP_AUTH_FAIL, "invalid or expired token");
        return;
    }
    auto user = db::find_user_by_id(*uid);
    if (!user) {
        send_error(wsi, OP_AUTH_FAIL, "user not found");
        return;
    }
    session.user_id  = user->id;
    session.username = user->username;
    session.authed   = true;

    json resp;
    resp["op"]       = OP_AUTH_OK;
    resp["user_id"]  = user->id;
    resp["username"] = user->username;
    enqueue(wsi, resp.dump());
}

static void handle_channel_join(lws* wsi, ws::Session& session, const json& msg) {
    int channel_id = msg.value("channel_id", 0);
    if (channel_id <= 0) { send_error(wsi, OP_ERROR, "invalid channel_id"); return; }
    session.subscribed_channels.insert(channel_id);
}

static void handle_channel_leave(lws* /*wsi*/, ws::Session& session, const json& msg) {
    int channel_id = msg.value("channel_id", 0);
    session.subscribed_channels.erase(channel_id);
}

static void handle_message_send(lws* wsi, ws::Session& session, const json& msg) {
    int channel_id = msg.value("channel_id", 0);
    std::string content = msg.value("content", "");

    if (channel_id <= 0 || content.empty()) {
        send_error(wsi, OP_ERROR, "invalid channel_id or empty content");
        return;
    }
    if (content.size() > MAX_MSG_LEN) content.resize(MAX_MSG_LEN);

    int64_t new_id = db::add_message(channel_id, session.user_id, content);
    if (new_id < 0) {
        send_error(wsi, OP_ERROR, "failed to save message");
        return;
    }

    json broadcast;
    broadcast["op"]         = OP_MESSAGE_NEW;
    broadcast["id"]         = new_id;
    broadcast["channel_id"] = channel_id;
    broadcast["author"]     = session.username;
    broadcast["content"]    = content;
    broadcast["ts"]         = (int64_t)time(nullptr);

    ws::broadcast_to_channel(channel_id, broadcast.dump());
}

static void dispatch(lws* wsi, ws::Session& session, const std::string& raw) {
    json msg;
    try {
        msg = json::parse(raw);
    } catch (...) {
        send_error(wsi, OP_ERROR, "malformed JSON");
        return;
    }

    std::string op = msg.value("op", "");

    // AUTH is the only op allowed before authentication
    if (op == OP_AUTH) { handle_auth(wsi, session, msg); return; }

    if (!session.authed) {
        send_error(wsi, OP_AUTH_FAIL, "not authenticated");
        return;
    }

    if      (op == OP_CHANNEL_JOIN)  handle_channel_join(wsi, session, msg);
    else if (op == OP_CHANNEL_LEAVE) handle_channel_leave(wsi, session, msg);
    else if (op == OP_MESSAGE_SEND)  handle_message_send(wsi, session, msg);
    else                             send_error(wsi, OP_ERROR, "unknown op");
}

// ─── lws callback ─────────────────────────────────────────────────────────────

static int ws_callback(lws* wsi, lws_callback_reasons reason,
                       void* /*user*/, void* in, size_t len) {
    switch (reason) {
    // ── Connection established ──────────────────────────────────────────────
    case LWS_CALLBACK_ESTABLISHED:
        g_sessions[wsi] = ws::Session{};
        fprintf(stdout, "[ws] client connected\n");
        break;

    // ── Connection closed ───────────────────────────────────────────────────
    case LWS_CALLBACK_CLOSED:
        g_sessions.erase(wsi);
        fprintf(stdout, "[ws] client disconnected\n");
        break;

    // ── Data received ───────────────────────────────────────────────────────
    case LWS_CALLBACK_RECEIVE: {
        auto it = g_sessions.find(wsi);
        if (it == g_sessions.end()) break;

        ws::Session& session = it->second;
        session.recv_buf.append(static_cast<char*>(in), len);

        if (!lws_is_final_fragment(wsi)) break; // wait for remaining fragments

        dispatch(wsi, session, session.recv_buf);
        session.recv_buf.clear();
        break;
    }

    // ── Ready to write ──────────────────────────────────────────────────────
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        auto it = g_sessions.find(wsi);
        if (it == g_sessions.end()) break;

        ws::Session& session = it->second;
        if (session.write_queue.empty()) break;

        const std::string& msg = session.write_queue.front();
        size_t msg_len = msg.size();

        // lws requires LWS_PRE bytes of padding before the payload
        std::vector<unsigned char> buf(LWS_PRE + msg_len);
        memcpy(buf.data() + LWS_PRE, msg.data(), msg_len);

        int written = lws_write(wsi,
                                buf.data() + LWS_PRE,
                                msg_len,
                                LWS_WRITE_TEXT);
        if (written < (int)msg_len)
            fprintf(stderr, "[ws] partial write\n");

        session.write_queue.pop_front();

        if (!session.write_queue.empty())
            lws_callback_on_writable(wsi); // flush remaining
        break;
    }

    default:
        break;
    }
    return 0;
}

// ─── Broadcast ────────────────────────────────────────────────────────────────

void ws::broadcast_to_channel(int channel_id, const std::string& json_msg) {
    for (auto& [wsi, session] : g_sessions) {
        if (session.authed && session.subscribed_channels.count(channel_id)) {
            session.write_queue.push_back(json_msg);
            lws_callback_on_writable(wsi);
        }
    }
}

// ─── Protocol descriptor ──────────────────────────────────────────────────────

lws_protocols ws::protocol = {
    "norichat",     // protocol name (client must send in Sec-WebSocket-Protocol)
    ws_callback,
    0,              // per_session_data_size (we use g_sessions instead)
    WS_RX_BUFFER,  // rx_buffer_size
    0, nullptr, 0
};
