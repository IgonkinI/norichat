#include "main_screen.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
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

void MainScreen::process_incoming(AppState& state, WsClient& ws) {
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

        if (op == "AUTH_OK") {
            // Mark already-online users from the snapshot in AUTH_OK
            if (msg.contains("online") && msg["online"].is_array()) {
                for (auto& u : msg["online"]) {
                    int uid = u.value("user_id", 0);
                    std::string uname = u.value("username", "");
                    bool found = false;
                    for (auto& m : state.members) {
                        if (m.id == uid) { m.online = true; found = true; break; }
                    }
                    if (!found && uid > 0)
                        state.members.push_back({uid, uname, true});
                }
            }
            // Mark self as online
            for (auto& m : state.members)
                if (m.id == state.user_id) { m.online = true; break; }

            // Now that AUTH is confirmed by server, join the pre-selected channel
            if (state.selected_channel_id >= 0) {
                json join;
                join["op"]         = "CHANNEL_JOIN";
                join["channel_id"] = state.selected_channel_id;
                ws.send(join.dump());
            }
            state.set_status("WebSocket authenticated");
        }
        else if (op == "USER_ONLINE") {
            int uid = msg.value("user_id", 0);
            std::string uname = msg.value("username", "");
            bool found = false;
            for (auto& m : state.members) {
                if (m.id == uid) { m.online = true; found = true; break; }
            }
            if (!found && uid > 0)
                state.members.push_back({uid, uname, true});
        }
        else if (op == "USER_OFFLINE") {
            int uid = msg.value("user_id", 0);
            for (auto& m : state.members)
                if (m.id == uid) { m.online = false; break; }
        }
        else if (op == "MESSAGE_NEW") {
            MessageInfo m;
            m.id         = msg.value("id", 0);
            m.channel_id = msg.value("channel_id", 0);
            m.author_id  = msg.value("author_id", 0);
            m.author     = msg.value("author", "?");
            m.content    = msg.value("content", "");
            m.ts         = msg.value("ts", (int64_t)0);

            if (m.channel_id == state.selected_channel_id) {
                std::lock_guard<std::mutex> lk(state.msg_mutex);
                state.messages.push_back(m);
                state.scroll_to_bottom = true;
            }
        }
        else if (op == "MESSAGE_EDITED") {
            int msg_id       = msg.value("message_id", 0);
            int ch_id        = msg.value("channel_id", 0);
            std::string cont = msg.value("content", "");
            if (ch_id == state.selected_channel_id) {
                std::lock_guard<std::mutex> lk(state.msg_mutex);
                for (auto& m : state.messages)
                    if (m.id == msg_id) { m.content = cont; break; }
            }
        }
        else if (op == "MESSAGE_DELETED") {
            int msg_id = msg.value("message_id", 0);
            int ch_id  = msg.value("channel_id", 0);
            if (ch_id == state.selected_channel_id) {
                std::lock_guard<std::mutex> lk(state.msg_mutex);
                auto& msgs = state.messages;
                msgs.erase(
                    std::remove_if(msgs.begin(), msgs.end(),
                        [msg_id](const MessageInfo& m){ return m.id == msg_id; }),
                    msgs.end());
            }
        }
        else if (op == "AUTH_FAIL" || op == "ERROR") {
            state.set_status(msg.value("error", "Server error"), true);
        }
    }
}

// ─── Data loading ─────────────────────────────────────────────────────────────

void MainScreen::load_members(AppState& state, HttpClient& http, int server_id) {
    auto resp = http.get("/api/members?server_id=" + std::to_string(server_id),
                         state.auth_token);
    if (!resp || resp->status_code != 200) return;

    try {
        auto arr = json::parse(resp->body);
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
            m.author_id  = o.value("author_id", 0);
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

    // Username – cyan accent
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.f), "  %s", state.username.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    for (auto& sv : state.servers) {
        bool is_selected_server = (sv.id == state.selected_server_id);
        ImGui::PushStyleColor(ImGuiCol_Header,
                              is_selected_server
                              ? ImVec4(0.8f, 0.4f, 0.0f, 0.5f)
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
                        if (state.selected_channel_id >= 0) {
                            json leave;
                            leave["op"]         = "CHANNEL_LEAVE";
                            leave["channel_id"] = state.selected_channel_id;
                            ws.send(leave.dump());
                        }
                        state.selected_channel_id = ch.id;
                        json join;
                        join["op"]         = "CHANNEL_JOIN";
                        join["channel_id"] = ch.id;
                        ws.send(join.dump());
                        load_messages(state, http, ch.id);
                        editing_msg_id_ = -1;
                    }
                }
            }
        }

        if (ImGui::IsItemClicked() && sv.id != state.selected_server_id) {
            state.selected_server_id  = sv.id;
            state.selected_channel_id = -1;
            state.messages.clear();
            editing_msg_id_ = -1;

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

void MainScreen::render_messages(AppState& state, WsClient& ws) {
    ImGuiIO& io = ImGui::GetIO();
    const float sidebar_w  = 220.f;
    const float members_w  = 160.f;
    const float input_h    = 40.f;
    const float topbar_h   = 28.f;
    const float msg_x      = sidebar_w;
    const float msg_y      = topbar_h;
    const float msg_w      = io.DisplaySize.x - sidebar_w - members_w;
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
                ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.f),
                                   "  # %s", ch.name.c_str());
                break;
            }
    } else {
        ImGui::TextDisabled("  Select a channel");
    }
    ImGui::End();

    // Messages window
    ImGui::SetNextWindowPos(ImVec2(msg_x, msg_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(msg_w, msg_h), ImGuiCond_Always);
    ImGui::Begin("##messages", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize   |
                 ImGuiWindowFlags_NoMove);

    // Pending WS actions (collected during render, sent after)
    int         pending_delete     = -1;
    int         pending_edit_id    = -1;
    std::string pending_edit_cont;

    if (state.selected_channel_id < 0) {
        ImGui::TextDisabled("Select a channel to start chatting.");
    } else {
        std::lock_guard<std::mutex> lk(state.msg_mutex);

        for (auto& m : state.messages) {
            // Author (cyan) + timestamp (dim)
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.f), "%s", m.author.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(" [%s]", format_ts(m.ts).c_str());

            bool own    = (m.author_id == state.user_id);
            bool recent = ((int64_t)time(nullptr) - m.ts) <= 7LL * 24 * 3600;

            if (editing_msg_id_ == m.id) {
                // Inline edit field
                ImGui::SetNextItemWidth(msg_w - 120.f);
                bool submit = ImGui::InputText("##ed", edit_buf_, sizeof(edit_buf_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                submit |= ImGui::SmallButton("OK");
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) editing_msg_id_ = -1;

                if (submit && strlen(edit_buf_) > 0) {
                    pending_edit_id   = m.id;
                    pending_edit_cont = edit_buf_;
                    editing_msg_id_   = -1;
                }
            } else {
                ImGui::TextWrapped("%s", m.content.c_str());

                // Right-click context menu for own recent messages
                if (own && recent) {
                    std::string popup_id = "##ctx" + std::to_string(m.id);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        ImGui::OpenPopup(popup_id.c_str());
                    if (ImGui::BeginPopup(popup_id.c_str())) {
                        if (ImGui::MenuItem("Edit")) {
                            editing_msg_id_ = m.id;
                            strncpy(edit_buf_, m.content.c_str(), sizeof(edit_buf_) - 1);
                            edit_buf_[sizeof(edit_buf_) - 1] = '\0';
                        }
                        if (ImGui::MenuItem("Delete"))
                            pending_delete = m.id;
                        ImGui::EndPopup();
                    }
                }
            }
        }

        if (state.scroll_to_bottom) {
            ImGui::SetScrollHereY(1.f);
            state.scroll_to_bottom = false;
        }
    } // release msg_mutex

    ImGui::End();

    // Send WS actions outside the lock
    if (pending_delete > 0) {
        json j;
        j["op"]         = "MESSAGE_DELETE";
        j["message_id"] = pending_delete;
        ws.send(j.dump());
    }
    if (pending_edit_id > 0) {
        json j;
        j["op"]         = "MESSAGE_EDIT";
        j["message_id"] = pending_edit_id;
        j["content"]    = pending_edit_cont;
        ws.send(j.dump());
    }
}

// ─── Message input ────────────────────────────────────────────────────────────

void MainScreen::render_input(AppState& state, WsClient& ws) {
    ImGuiIO& io = ImGui::GetIO();
    const float sidebar_w = 220.f;
    const float members_w = 160.f;
    const float input_h   = 40.f;

    ImGui::SetNextWindowPos(ImVec2(sidebar_w, io.DisplaySize.y - input_h),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - sidebar_w - members_w, input_h),
                             ImGuiCond_Always);
    ImGui::Begin("##input", nullptr,
                 ImGuiWindowFlags_NoTitleBar  |
                 ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove      |
                 ImGuiWindowFlags_NoScrollbar);

    bool disabled = (state.selected_channel_id < 0 || !ws.is_connected());
    if (disabled) ImGui::BeginDisabled();

    ImGui::SetNextItemWidth(io.DisplaySize.x - sidebar_w - members_w - 80.f);
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
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

// ─── Members panel ────────────────────────────────────────────────────────────

void MainScreen::render_members(AppState& state) {
    ImGuiIO& io = ImGui::GetIO();
    const float members_w = 160.f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - members_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(members_w, io.DisplaySize.y), ImGuiCond_Always);
    ImGui::Begin("##members_panel", nullptr,
                 ImGuiWindowFlags_NoTitleBar  |
                 ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove);

    ImGui::TextDisabled(" Members");
    ImGui::Separator();
    ImGui::Spacing();

    bool printed_online  = false;
    bool printed_offline = false;
    for (auto& m : state.members) {
        if (!m.online) continue;
        if (!printed_online) {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 0.6f), "  ONLINE");
            printed_online = true;
        }
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.f), "  * %s", m.username.c_str());
    }
    ImGui::Spacing();
    for (auto& m : state.members) {
        if (m.online) continue;
        if (!printed_offline) {
            ImGui::TextDisabled("  OFFLINE");
            printed_offline = true;
        }
        ImGui::TextDisabled("    %s", m.username.c_str());
    }

    ImGui::End();
}

// ─── Top-level update ─────────────────────────────────────────────────────────

void MainScreen::update(AppState& state, HttpClient& http, WsClient& ws) {
    static int last_server_id = -1;
    if (state.selected_server_id != last_server_id) {
        last_server_id = state.selected_server_id;
        if (state.selected_server_id >= 0)
            load_members(state, http, state.selected_server_id);
    }

    process_incoming(state, ws);
    render_sidebar(state, http, ws);
    render_messages(state, ws);
    render_input(state, ws);
    render_members(state);
}
