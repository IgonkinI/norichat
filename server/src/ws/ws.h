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
    std::set<int>           voice_channels;  // voice channels this session is in
    std::deque<std::string> write_queue;
    std::string             recv_buf;   // accumulate WebSocket fragments
};

// Send `json_msg` to all sessions subscribed to text `channel_id`.
void broadcast_to_channel(int channel_id, const std::string& json_msg);

// Send `json_msg` to all sessions in voice `channel_id`, excluding `exclude_wsi`.
void broadcast_to_voice(int channel_id, const std::string& json_msg,
                        lws* exclude_wsi = nullptr);

// lws protocol entry – must be included in the protocols[] array.
extern lws_protocols protocol;

} // namespace ws
