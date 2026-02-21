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

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.f;
    style.FrameRounding     = 3.f;
    style.ScrollbarRounding = 3.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.f);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ── App objects ──────────────────────────────────────────────────────────
    AppState state;
    std::unique_ptr<HttpClient> http =
        std::make_unique<HttpClient>(state.server_host, state.server_port);
    WsClient    ws;
    LoginScreen login_screen;
    MainScreen  main_screen;

    const ImVec4 clear_color(0.06f, 0.07f, 0.08f, 1.f);
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
