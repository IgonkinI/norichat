#pragma once
#include <libwebsockets.h>
#include <string>
#include <set>
#include <deque>
#include <map>

namespace ws {

// Per-connection session data (stored in a global map, keyed by lws*)
struct Session {
    int         user_id   = 0;
    std::string username;
    bool        authed    = false;
    std::set<int>           subscribed_channels;
    std::deque<std::string> write_queue;
    std::string             recv_buf;   // accumulate WebSocket fragments
};

// Send `json_msg` to all sessions subscribed to `channel_id`.
void broadcast_to_channel(int channel_id, const std::string& json_msg);

// lws protocol entry – must be included in the protocols[] array.
extern lws_protocols protocol;

} // namespace ws
