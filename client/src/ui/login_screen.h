#pragma once
#include "../state.h"
#include "../net/http_client.h"
#include "../net/ws_client.h"

class LoginScreen {
public:
    // Render the login/register UI.
    // If login/register succeeds, transitions state to Screen::Main.
    void render(AppState& state, HttpClient& http, WsClient& ws);

private:
    char username_buf_[64]  = {};
    char password_buf_[128] = {};
    bool busy_              = false;    // prevent double-click

    void do_login(AppState& state, HttpClient& http, WsClient& ws);
    void do_register(AppState& state, HttpClient& http, WsClient& ws);
    void on_auth_success(AppState& state, HttpClient& http,
                         WsClient& ws, const std::string& token,
                         int user_id, const std::string& username);
};
