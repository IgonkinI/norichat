#pragma once
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>

// ─── Domain types (mirror of server-side structs) ─────────────────────────────

struct ServerInfo {
    int         id       = 0;
    std::string name;
};

struct ChannelInfo {
    int         id        = 0;
    int         server_id = 0;
    std::string name;
    std::string type;
};

struct MessageInfo {
    int         id         = 0;
    int         channel_id = 0;
    int         author_id  = 0;
    std::string author;
    std::string content;
    int64_t     ts         = 0;
};

struct MemberInfo {
    int         id     = 0;
    std::string username;
    bool        online = false;
};

struct VoiceParticipant {
    int         user_id  = 0;
    std::string username;
};

// ─── Application state ────────────────────────────────────────────────────────

struct AppState {
    enum class Screen { Login, Main } screen = Screen::Login;

    // Connection settings (editable in UI)
    char server_host[128] = "127.0.0.1";
    int  server_port      = 8080;

    // Auth
    std::string auth_token;
    int         user_id  = 0;
    std::string username;

    // Selected IDs
    int selected_server_id  = -1;
    int selected_channel_id = -1;

    // Loaded data
    std::vector<ServerInfo>  servers;
    std::vector<ChannelInfo> channels;
    std::vector<MemberInfo>  members;   // all members of selected server

    // Messages – guarded by msg_mutex (written from WS thread)
    std::mutex               msg_mutex;
    std::vector<MessageInfo> messages;
    bool                     scroll_to_bottom = false;

    // Pending WS messages (raw JSON strings from the receive thread)
    std::mutex              incoming_mutex;
    std::deque<std::string> incoming_ws;

    // Voice state
    int                          voice_channel_id = -1; // -1 = not in voice
    std::vector<VoiceParticipant> voice_participants;

    // Status/error message shown in UI
    std::string status_msg;
    bool        status_is_error = false;

    void set_status(const std::string& msg, bool is_error = false) {
        status_msg      = msg;
        status_is_error = is_error;
    }
};
