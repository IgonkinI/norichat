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

    // Build list of currently online users for the new client
    json online_list = json::array();
    for (auto& [other_wsi, other_sess] : g_sessions) {
        if (other_sess.authed && other_wsi != wsi) {
            json u;
            u["user_id"]  = other_sess.user_id;
            u["username"] = other_sess.username;
            online_list.push_back(u);
        }
    }

    json resp;
    resp["op"]       = OP_AUTH_OK;
    resp["user_id"]  = user->id;
    resp["username"] = user->username;
    resp["online"]   = online_list;
    enqueue(wsi, resp.dump());

    // Notify all other authed sessions that this user came online
    json notify;
    notify["op"]       = OP_USER_ONLINE;
    notify["user_id"]  = user->id;
    notify["username"] = user->username;
    std::string notify_json = notify.dump();
    for (auto& [other_wsi, other_sess] : g_sessions) {
        if (other_sess.authed && other_wsi != wsi)
            enqueue(other_wsi, notify_json);
    }
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
    broadcast["author_id"]  = session.user_id;
    broadcast["author"]     = session.username;
    broadcast["content"]    = content;
    broadcast["ts"]         = (int64_t)time(nullptr);

    ws::broadcast_to_channel(channel_id, broadcast.dump());
}

static void handle_message_edit(lws* wsi, ws::Session& session, const json& msg) {
    int msg_id = msg.value("message_id", 0);
    std::string content = msg.value("content", "");
    if (msg_id <= 0 || content.empty()) {
        send_error(wsi, OP_ERROR, "invalid message_id or empty content");
        return;
    }
    if (content.size() > MAX_MSG_LEN) content.resize(MAX_MSG_LEN);

    auto orig = db::get_message_by_id(msg_id);
    if (!orig || orig->author_id != session.user_id) {
        send_error(wsi, OP_ERROR, "message not found or not yours");
        return;
    }
    if (!db::update_message(msg_id, session.user_id, content)) {
        send_error(wsi, OP_ERROR, "cannot edit: too old or not found");
        return;
    }

    json bcast;
    bcast["op"]         = OP_MESSAGE_EDITED;
    bcast["message_id"] = msg_id;
    bcast["channel_id"] = orig->channel_id;
    bcast["content"]    = content;
    ws::broadcast_to_channel(orig->channel_id, bcast.dump());
}

static void handle_message_delete(lws* wsi, ws::Session& session, const json& msg) {
    int msg_id = msg.value("message_id", 0);
    if (msg_id <= 0) {
        send_error(wsi, OP_ERROR, "invalid message_id");
        return;
    }
    auto orig = db::get_message_by_id(msg_id);
    if (!orig || orig->author_id != session.user_id) {
        send_error(wsi, OP_ERROR, "message not found or not yours");
        return;
    }
    if (!db::delete_message(msg_id, session.user_id)) {
        send_error(wsi, OP_ERROR, "cannot delete: too old or not found");
        return;
    }

    json bcast;
    bcast["op"]         = OP_MESSAGE_DELETED;
    bcast["message_id"] = msg_id;
    bcast["channel_id"] = orig->channel_id;
    ws::broadcast_to_channel(orig->channel_id, bcast.dump());
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

    if      (op == OP_CHANNEL_JOIN)    handle_channel_join(wsi, session, msg);
    else if (op == OP_CHANNEL_LEAVE)   handle_channel_leave(wsi, session, msg);
    else if (op == OP_MESSAGE_SEND)    handle_message_send(wsi, session, msg);
    else if (op == OP_MESSAGE_EDIT)    handle_message_edit(wsi, session, msg);
    else if (op == OP_MESSAGE_DELETE)  handle_message_delete(wsi, session, msg);
    else                               send_error(wsi, OP_ERROR, "unknown op");
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
    case LWS_CALLBACK_CLOSED: {
        auto it = g_sessions.find(wsi);
        if (it != g_sessions.end()) {
            if (it->second.authed) {
                // Notify remaining sessions that this user went offline
                json notify;
                notify["op"]      = OP_USER_OFFLINE;
                notify["user_id"] = it->second.user_id;
                std::string notify_json = notify.dump();
                for (auto& [other_wsi, other_sess] : g_sessions) {
                    if (other_sess.authed && other_wsi != wsi)
                        enqueue(other_wsi, notify_json);
                }
            }
            g_sessions.erase(it);
        }
        fprintf(stdout, "[ws] client disconnected\n");
        break;
    }

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
