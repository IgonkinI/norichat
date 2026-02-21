#include "main_screen.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

using json = nlohmann::json;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::string format_ts(int64_t ts) {
    time_t t = (time_t)ts;
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min);
    return buf;
}

// ─── Incoming WS message processing ──────────────────────────────────────────

void MainScreen::process_incoming(AppState& state) {
    std::deque<std::string> queue;
    {
        std::lock_guard<std::mutex> lk(state.incoming_mutex);
        queue.swap(state.incoming_ws);
    }

    for (auto& raw : queue) {
        json msg;
        try { msg = json::parse(raw); }
        catch (...) { continue; }

        std::string op = msg.value("op", "");

        if (op == "MESSAGE_NEW") {
            MessageInfo m;
            m.id         = msg.value("id", 0);
            m.channel_id = msg.value("channel_id", 0);
            m.author     = msg.value("author", "?");
            m.content    = msg.value("content", "");
            m.ts         = msg.value("ts", (int64_t)0);

            if (m.channel_id == state.selected_channel_id) {
                std::lock_guard<std::mutex> lk(state.msg_mutex);
                state.messages.push_back(m);
                state.scroll_to_bottom = true;
            }
        }
        else if (op == "AUTH_OK") {
            state.set_status("WebSocket authenticated");
        }
        else if (op == "AUTH_FAIL" || op == "ERROR") {
            state.set_status(msg.value("error", "Server error"), true);
        }
    }
}

// ─── Data loading ─────────────────────────────────────────────────────────────

void MainScreen::load_messages(AppState& state, HttpClient& http, int channel_id) {
    auto resp = http.get("/api/messages?channel_id=" + std::to_string(channel_id) +
                         "&limit=50", state.auth_token);
    std::lock_guard<std::mutex> lk(state.msg_mutex);
    state.messages.clear();
    if (!resp || resp->status_code != 200) return;

    try {
        auto arr = json::parse(resp->body);
        for (auto& o : arr) {
            MessageInfo m;
            m.id         = o.value("id", 0);
            m.channel_id = o.value("channel_id", 0);
            m.author     = o.value("author", "?");
            m.content    = o.value("content", "");
            m.ts         = o.value("ts", (int64_t)0);
            state.messages.push_back(m);
        }
    } catch (...) {}
    state.scroll_to_bottom = true;
}

// ─── Sidebar (servers + channels) ────────────────────────────────────────────

void MainScreen::render_sidebar(AppState& state, HttpClient& http, WsClient& ws) {
    ImGuiIO& io = ImGui::GetIO();
    const float sidebar_w = 220.f;
    const float total_h   = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sidebar_w, total_h), ImGuiCond_Always);
    ImGui::Begin("##sidebar",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar  |
                 ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove      |
                 ImGuiWindowFlags_NoScrollbar);

    // Username
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.f, 1.f),
                       "  %s", state.username.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    for (auto& sv : state.servers) {
        bool is_selected_server = (sv.id == state.selected_server_id);
        ImGui::PushStyleColor(ImGuiCol_Header,
                              is_selected_server
                              ? ImVec4(0.2f, 0.4f, 0.8f, 0.5f)
                              : ImVec4(0, 0, 0, 0));

        bool open = ImGui::CollapsingHeader(sv.name.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();

        if (open) {
            for (auto& ch : state.channels) {
                if (ch.server_id != sv.id) continue;

                std::string label = "  # " + ch.name;
                bool sel = (ch.id == state.selected_channel_id);

                if (ImGui::Selectable(label.c_str(), sel,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(sidebar_w - 16.f, 0))) {
                    if (!sel) {
                        // Leave old channel
                        if (state.selected_channel_id >= 0) {
                            json leave;
                            leave["op"]         = "CHANNEL_LEAVE";
                            leave["channel_id"] = state.selected_channel_id;
                            ws.send(leave.dump());
                        }
                        // Join new channel
                        state.selected_channel_id = ch.id;
                        json join;
                        join["op"]         = "CHANNEL_JOIN";
                        join["channel_id"] = ch.id;
                        ws.send(join.dump());

                        // Load history
                        load_messages(state, http, ch.id);
                    }
                }
            }
        }

        // Load channels when a different server is clicked
        if (is_selected_server != (sv.id == state.selected_server_id)) {
            // selection changed – handled below
        }

        if (ImGui::IsItemClicked() && sv.id != state.selected_server_id) {
            state.selected_server_id  = sv.id;
            state.selected_channel_id = -1;
            state.messages.clear();

            auto resp = http.get("/api/channels?server_id=" +
                                 std::to_string(sv.id), state.auth_token);
            state.channels.clear();
            if (resp && resp->status_code == 200) {
                try {
                    auto arr = json::parse(resp->body);
                    for (auto& o : arr)
                        state.channels.push_back({o["id"].get<int>(),
                                                  o["server_id"].get<int>(),
                                                  o["name"].get<std::string>(),
                                                  o["type"].get<std::string>()});
                } catch (...) {}
            }
        }
    }

    ImGui::End();
}

// ─── Message list ─────────────────────────────────────────────────────────────

void MainScreen::render_messages(AppState& state) {
    ImGuiIO& io = ImGui::GetIO();
    const float sidebar_w  = 220.f;
    const float input_h    = 40.f;
    const float topbar_h   = 28.f;
    const float msg_x      = sidebar_w;
    const float msg_y      = topbar_h;
    const float msg_w      = io.DisplaySize.x - sidebar_w;
    const float msg_h      = io.DisplaySize.y - topbar_h - input_h;

    // Top bar: channel name
    ImGui::SetNextWindowPos(ImVec2(msg_x, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(msg_w, topbar_h), ImGuiCond_Always);
    ImGui::Begin("##topbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar  |
                 ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove      |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);

    if (state.selected_channel_id >= 0) {
        for (auto& ch : state.channels)
            if (ch.id == state.selected_channel_id) {
                ImGui::Text("  # %s", ch.name.c_str());
                break;
            }
    } else {
        ImGui::TextDisabled("  Select a channel");
    }
    ImGui::End();

    // Messages
    ImGui::SetNextWindowPos(ImVec2(msg_x, msg_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(msg_w, msg_h), ImGuiCond_Always);
    ImGui::Begin("##messages", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize   |
                 ImGuiWindowFlags_NoMove);

    if (state.selected_channel_id < 0) {
        ImGui::TextDisabled("Select a channel to start chatting.");
    } else {
        std::lock_guard<std::mutex> lk(state.msg_mutex);
        for (auto& m : state.messages) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.f, 1.f),
                               "%s", m.author.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", format_ts(m.ts).c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", m.content.c_str());
        }
        if (state.scroll_to_bottom) {
            ImGui::SetScrollHereY(1.f);
            state.scroll_to_bottom = false;
        }
    }

    ImGui::End();
}

// ─── Message input ────────────────────────────────────────────────────────────

void MainScreen::render_input(AppState& state, WsClient& ws) {
    ImGuiIO& io = ImGui::GetIO();
    const float sidebar_w = 220.f;
    const float input_h   = 40.f;

    ImGui::SetNextWindowPos(ImVec2(sidebar_w, io.DisplaySize.y - input_h),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - sidebar_w, input_h),
                             ImGuiCond_Always);
    ImGui::Begin("##input", nullptr,
                 ImGuiWindowFlags_NoTitleBar  |
                 ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove      |
                 ImGuiWindowFlags_NoScrollbar);

    bool disabled = (state.selected_channel_id < 0 || !ws.is_connected());
    if (disabled) ImGui::BeginDisabled();

    ImGui::SetNextItemWidth(io.DisplaySize.x - sidebar_w - 80.f);
    bool send = ImGui::InputText("##msg_input", input_buf_, sizeof(input_buf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    send |= ImGui::Button("Send", ImVec2(60.f, 0));

    if (disabled) ImGui::EndDisabled();

    if (send && !disabled && strlen(input_buf_) > 0) {
        json msg;
        msg["op"]         = "MESSAGE_SEND";
        msg["channel_id"] = state.selected_channel_id;
        msg["content"]    = std::string(input_buf_);
        ws.send(msg.dump());
        input_buf_[0] = '\0';

        // Re-focus the input field
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

// ─── Top-level update ─────────────────────────────────────────────────────────

void MainScreen::update(AppState& state, HttpClient& http, WsClient& ws) {
    process_incoming(state);
    render_sidebar(state, http, ws);
    render_messages(state);
    render_input(state, ws);
}
