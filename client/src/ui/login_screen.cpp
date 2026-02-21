#include "login_screen.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>

using json = nlohmann::json;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void load_servers(AppState& state, HttpClient& http) {
    auto resp = http.get("/api/servers", state.auth_token);
    if (!resp || resp->status_code != 200) {
        state.set_status("Failed to load servers", true);
        return;
    }
    try {
        auto arr = json::parse(resp->body);
        state.servers.clear();
        for (auto& o : arr)
            state.servers.push_back({o["id"].get<int>(), o["name"].get<std::string>()});
    } catch (...) {
        state.set_status("Malformed server list", true);
    }
}

// ─── LoginScreen ──────────────────────────────────────────────────────────────

void LoginScreen::on_auth_success(AppState& state, HttpClient& http,
                                  WsClient& ws,
                                  const std::string& token,
                                  int user_id,
                                  const std::string& username) {
    state.auth_token = token;
    state.user_id    = user_id;
    state.username   = username;

    // Connect WebSocket
    if (!ws.connect(state.server_host, state.server_port, token)) {
        state.set_status("Server online but WebSocket failed", true);
        return;
    }

    // Set up WS message → incoming queue
    ws.set_on_message([&state](const std::string& msg) {
        std::lock_guard<std::mutex> lk(state.incoming_mutex);
        state.incoming_ws.push_back(msg);
    });

    // Load initial server list
    load_servers(state, http);

    // Select first server and load its channels + members
    if (!state.servers.empty()) {
        state.selected_server_id = state.servers[0].id;

        // Load channels
        auto ch_resp = http.get("/api/channels?server_id=" +
                                std::to_string(state.selected_server_id),
                                state.auth_token);
        if (ch_resp && ch_resp->status_code == 200) {
            try {
                auto arr = json::parse(ch_resp->body);
                state.channels.clear();
                for (auto& o : arr)
                    state.channels.push_back({o["id"].get<int>(),
                                              o["server_id"].get<int>(),
                                              o["name"].get<std::string>(),
                                              o["type"].get<std::string>()});
            } catch (...) {}
        }

        // Auto-join first text channel
        for (auto& ch : state.channels) {
            if (ch.server_id == state.selected_server_id) {
                state.selected_channel_id = ch.id;
                json join;
                join["op"]         = "CHANNEL_JOIN";
                join["channel_id"] = ch.id;
                ws.send(join.dump());

                // Load message history
                auto msg_resp = http.get("/api/messages?channel_id=" +
                                         std::to_string(ch.id) + "&limit=50",
                                         state.auth_token);
                if (msg_resp && msg_resp->status_code == 200) {
                    try {
                        auto arr = json::parse(msg_resp->body);
                        state.messages.clear();
                        for (auto& o : arr) {
                            MessageInfo m;
                            m.id         = o.value("id", 0);
                            m.channel_id = o.value("channel_id", 0);
                            m.author     = o.value("author", "?");
                            m.content    = o.value("content", "");
                            m.ts         = o.value("ts", (int64_t)0);
                            state.messages.push_back(m);
                        }
                        state.scroll_to_bottom = true;
                    } catch (...) {}
                }
                break; // only join the first channel
            }
        }

        // Load server members
        auto mem_resp = http.get("/api/members?server_id=" +
                                  std::to_string(state.selected_server_id),
                                  state.auth_token);
        if (mem_resp && mem_resp->status_code == 200) {
            try {
                auto arr = json::parse(mem_resp->body);
                state.members.clear();
                for (auto& o : arr) {
                    MemberInfo m;
                    m.id       = o.value("id", 0);
                    m.username = o.value("username", "?");
                    m.online   = (m.id == state.user_id);
                    state.members.push_back(m);
                }
            } catch (...) {}
        }
    }

    state.set_status("Connected as " + username);
    state.screen = AppState::Screen::Main;
}

void LoginScreen::do_login(AppState& state, HttpClient& http, WsClient& ws) {
    if (busy_) return;
    std::string uname(username_buf_);
    std::string passwd(password_buf_);
    if (uname.empty() || passwd.empty()) {
        state.set_status("Enter username and password", true);
        return;
    }

    busy_ = true;
    json body;
    body["username"] = uname;
    body["password"] = passwd;

    auto resp = http.post("/api/login", body.dump());
    busy_ = false;

    if (!resp) {
        state.set_status("Cannot reach server", true);
        return;
    }
    if (resp->status_code != 200) {
        try {
            auto err = json::parse(resp->body);
            state.set_status(err.value("error", "Login failed"), true);
        } catch (...) { state.set_status("Login failed", true); }
        return;
    }
    try {
        auto j = json::parse(resp->body);
        on_auth_success(state, http, ws,
                        j["token"].get<std::string>(),
                        j["user_id"].get<int>(),
                        j["username"].get<std::string>());
    } catch (...) {
        state.set_status("Malformed response", true);
    }
}

void LoginScreen::do_register(AppState& state, HttpClient& http, WsClient& ws) {
    if (busy_) return;
    std::string uname(username_buf_);
    std::string passwd(password_buf_);
    if (uname.empty() || passwd.empty()) {
        state.set_status("Enter username and password", true);
        return;
    }

    busy_ = true;
    json body;
    body["username"] = uname;
    body["password"] = passwd;

    auto resp = http.post("/api/register", body.dump());
    busy_ = false;

    if (!resp) {
        state.set_status("Cannot reach server", true);
        return;
    }
    if (resp->status_code != 201) {
        try {
            auto err = json::parse(resp->body);
            state.set_status(err.value("error", "Register failed"), true);
        } catch (...) { state.set_status("Register failed", true); }
        return;
    }
    try {
        auto j = json::parse(resp->body);
        on_auth_success(state, http, ws,
                        j["token"].get<std::string>(),
                        j["user_id"].get<int>(),
                        j["username"].get<std::string>());
    } catch (...) {
        state.set_status("Malformed response", true);
    }
}

void LoginScreen::render(AppState& state, HttpClient& http, WsClient& ws) {
    ImGuiIO& io = ImGui::GetIO();
    const float W = 380.f, H = 280.f;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - W) * 0.5f,
                                  (io.DisplaySize.y - H) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("NoriChat",
                 nullptr,
                 ImGuiWindowFlags_NoResize  |
                 ImGuiWindowFlags_NoMove    |
                 ImGuiWindowFlags_NoCollapse);

    // Title
    ImGui::SetCursorPosX((W - ImGui::CalcTextSize("NoriChat").x) * 0.5f - 8.f);
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.f, 1.f), "NoriChat");
    ImGui::Separator();
    ImGui::Spacing();

    // Server address fields
    ImGui::Text("Server");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.f);
    ImGui::InputText("##host", state.server_host, sizeof(state.server_host));
    ImGui::SameLine();
    ImGui::Text(":");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.f);
    ImGui::InputInt("##port", &state.server_port, 0);
    if (state.server_port < 1) state.server_port = 1;
    if (state.server_port > 65535) state.server_port = 65535;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(W - 30.f);
    bool enter = ImGui::InputText("Username##u", username_buf_,
                                  sizeof(username_buf_),
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetNextItemWidth(W - 30.f);
    enter |= ImGui::InputText("Password##p", password_buf_,
                              sizeof(password_buf_),
                              ImGuiInputTextFlags_Password |
                              ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Spacing();

    float btn_w = (W - 30.f) * 0.5f - 4.f;
    if (ImGui::Button("Login##btn", ImVec2(btn_w, 0)) || enter)
        do_login(state, http, ws);
    ImGui::SameLine();
    if (ImGui::Button("Register##btn", ImVec2(btn_w, 0)))
        do_register(state, http, ws);

    // Status
    if (!state.status_msg.empty()) {
        ImGui::Spacing();
        if (state.status_is_error)
            ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                               "%s", state.status_msg.c_str());
        else
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f),
                               "%s", state.status_msg.c_str());
    }

    ImGui::End();
}
