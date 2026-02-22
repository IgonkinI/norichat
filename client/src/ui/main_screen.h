#pragma once
#include "../state.h"
#include "../net/http_client.h"
#include "../net/ws_client.h"

class MainScreen {
public:
    // Process any pending WebSocket messages and render the main UI.
    void update(AppState& state, HttpClient& http, WsClient& ws);

private:
    char input_buf_[2000] = {};
    int  editing_msg_id_  = -1;
    char edit_buf_[4001]  = {};

    // Create channel dialog state
    bool show_create_channel_ = false;
    int  create_channel_server_id_ = -1;
    char new_channel_buf_[65] = {};

    void process_incoming(AppState& state, WsClient& ws);
    void render_sidebar(AppState& state, HttpClient& http, WsClient& ws);
    void render_messages(AppState& state, WsClient& ws);
    void render_input(AppState& state, WsClient& ws);
    void render_members(AppState& state);
    void load_messages(AppState& state, HttpClient& http, int channel_id);
    void load_members(AppState& state, HttpClient& http, int server_id);
};
