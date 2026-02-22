// NoriChat Client – SDL2 + OpenGL3 + Dear ImGui
// Cross-platform: Windows and Linux.

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <memory>
#include "state.h"
#include "net/http_client.h"
#include "net/ws_client.h"
#include "ui/login_screen.h"
#include "ui/main_screen.h"

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[SDL] Init error: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL 3.3 Core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "NoriChat",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "[SDL] CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "[SDL] GL context error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1); // VSync

    // ── ImGui setup ──────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr; // no imgui.ini

    // ── LCARS / Star Trek theme ──────────────────────────────────────────────
    ImGui::StyleColorsDark(); // baseline reset
    ImGuiStyle& style = ImGui::GetStyle();

    // Geometry – mostly sharp, a touch of rounding on frames
    style.WindowRounding    = 2.f;
    style.ChildRounding     = 2.f;
    style.FrameRounding     = 2.f;
    style.PopupRounding     = 2.f;
    style.ScrollbarRounding = 2.f;
    style.GrabRounding      = 2.f;
    style.TabRounding       = 0.f;
    style.WindowBorderSize  = 1.f;
    style.FrameBorderSize   = 0.f;
    style.ItemSpacing       = ImVec2(8.f, 5.f);
    style.FramePadding      = ImVec2(6.f, 3.f);
    style.WindowPadding     = ImVec2(8.f, 8.f);
    style.ScrollbarSize     = 10.f;

    // Palette
    const ImVec4 bg_deep   (0.04f, 0.07f, 0.12f, 1.f);
    const ImVec4 bg_mid    (0.07f, 0.11f, 0.18f, 1.f);
    const ImVec4 bg_lift   (0.10f, 0.16f, 0.26f, 1.f);
    const ImVec4 amber     (1.00f, 0.60f, 0.00f, 1.f);
    const ImVec4 amber_hi  (1.00f, 0.76f, 0.15f, 1.f);
    const ImVec4 amber_lo  (0.85f, 0.44f, 0.00f, 1.f);
    const ImVec4 cyan      (0.00f, 0.72f, 0.90f, 1.f);
    const ImVec4 cyan_dim  (0.00f, 0.45f, 0.60f, 0.6f);
    const ImVec4 txt       (0.90f, 0.95f, 1.00f, 1.f);
    const ImVec4 txt_dim   (0.50f, 0.62f, 0.74f, 1.f);
    const ImVec4 border    (0.14f, 0.28f, 0.44f, 1.f);
    const ImVec4 none      (0.f, 0.f, 0.f, 0.f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = txt;
    c[ImGuiCol_TextDisabled]          = txt_dim;
    c[ImGuiCol_WindowBg]              = bg_deep;
    c[ImGuiCol_ChildBg]               = bg_mid;
    c[ImGuiCol_PopupBg]               = ImVec4(0.05f, 0.09f, 0.15f, 0.97f);
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = none;
    c[ImGuiCol_FrameBg]               = bg_lift;
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.14f, 0.22f, 0.36f, 1.f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.18f, 0.28f, 0.44f, 1.f);
    c[ImGuiCol_TitleBg]               = bg_mid;
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.06f, 0.10f, 0.20f, 1.f);
    c[ImGuiCol_TitleBgCollapsed]      = bg_deep;
    c[ImGuiCol_MenuBarBg]             = bg_mid;
    c[ImGuiCol_ScrollbarBg]           = bg_mid;
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.90f, 0.55f, 0.00f, 0.60f);
    c[ImGuiCol_ScrollbarGrabHovered]  = amber;
    c[ImGuiCol_ScrollbarGrabActive]   = amber_lo;
    c[ImGuiCol_CheckMark]             = amber;
    c[ImGuiCol_SliderGrab]            = amber;
    c[ImGuiCol_SliderGrabActive]      = amber_hi;
    c[ImGuiCol_Button]                = ImVec4(1.00f, 0.60f, 0.00f, 0.85f);
    c[ImGuiCol_ButtonHovered]         = amber_hi;
    c[ImGuiCol_ButtonActive]          = amber_lo;
    c[ImGuiCol_Header]                = ImVec4(0.00f, 0.55f, 0.72f, 0.45f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.00f, 0.65f, 0.85f, 0.55f);
    c[ImGuiCol_HeaderActive]          = cyan;
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = cyan_dim;
    c[ImGuiCol_SeparatorActive]       = cyan;
    c[ImGuiCol_ResizeGrip]            = ImVec4(1.00f, 0.60f, 0.00f, 0.25f);
    c[ImGuiCol_ResizeGripHovered]     = amber;
    c[ImGuiCol_ResizeGripActive]      = amber_hi;
    c[ImGuiCol_Tab]                   = bg_mid;
    c[ImGuiCol_TabHovered]            = ImVec4(0.00f, 0.55f, 0.70f, 0.5f);
    c[ImGuiCol_TabActive]             = ImVec4(0.00f, 0.50f, 0.65f, 1.f);
    c[ImGuiCol_TabUnfocused]          = bg_deep;
    c[ImGuiCol_TabUnfocusedActive]    = bg_mid;
    c[ImGuiCol_PlotLines]             = cyan;
    c[ImGuiCol_PlotLinesHovered]      = amber;
    c[ImGuiCol_PlotHistogram]         = cyan;
    c[ImGuiCol_PlotHistogramHovered]  = amber;
    c[ImGuiCol_TableHeaderBg]         = bg_mid;
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.10f, 0.18f, 0.28f, 1.f);
    c[ImGuiCol_TableRowBg]            = none;
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(0.07f, 0.11f, 0.16f, 0.5f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.00f, 0.55f, 0.80f, 0.35f);
    c[ImGuiCol_DragDropTarget]        = amber;
    c[ImGuiCol_NavHighlight]          = cyan;
    c[ImGuiCol_NavWindowingHighlight] = amber;
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.04f, 0.07f, 0.12f, 0.70f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.04f, 0.07f, 0.12f, 0.70f);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ── App objects ──────────────────────────────────────────────────────────
    AppState state;
    std::unique_ptr<HttpClient> http =
        std::make_unique<HttpClient>(state.server_host, state.server_port);
    WsClient    ws;
    LoginScreen login_screen;
    MainScreen  main_screen;

    const ImVec4 clear_color(0.04f, 0.07f, 0.12f, 1.f); // LCARS deep navy
    bool running = true;

    // ── Main loop ────────────────────────────────────────────────────────────
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        // Re-create HttpClient if server address changed
        {
            static char last_host[128] = {};
            static int  last_port      = 0;
            if (strcmp(last_host, state.server_host) != 0 ||
                last_port != state.server_port) {
                strncpy(last_host, state.server_host, sizeof(last_host) - 1);
                last_port = state.server_port;
                http = std::make_unique<HttpClient>(state.server_host,
                                                    state.server_port);
            }
        }

        // ImGui new frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Fullscreen backdrop
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::SetNextWindowBgAlpha(0.f);
            ImGui::Begin("##backdrop", nullptr,
                         ImGuiWindowFlags_NoTitleBar  |
                         ImGuiWindowFlags_NoResize    |
                         ImGuiWindowFlags_NoMove      |
                         ImGuiWindowFlags_NoInputs    |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::End();
        }

        // Dispatch to current screen
        switch (state.screen) {
        case AppState::Screen::Login:
            login_screen.render(state, *http, ws);
            break;
        case AppState::Screen::Main:
            main_screen.update(state, *http, ws);
            break;
        }

        // Render
        ImGui::Render();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(clear_color.x, clear_color.y,
                     clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ── Cleanup ──────────────────────────────────────────────────────────────
    ws.disconnect();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
