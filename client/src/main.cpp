// NoriChat Client – Win32 + DirectX 11 + Dear ImGui
// Requires Windows SDK (D3D11 / DXGI) and Visual C++ / MinGW.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <memory>
#include "state.h"
#include "net/http_client.h"
#include "net/ws_client.h"
#include "ui/login_screen.h"
#include "ui/main_screen.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winhttp.lib")

// ─── DirectX 11 globals ───────────────────────────────────────────────────────

static ID3D11Device*           g_device           = nullptr;
static ID3D11DeviceContext*    g_device_ctx        = nullptr;
static IDXGISwapChain*         g_swap_chain        = nullptr;
static ID3D11RenderTargetView* g_main_rtv          = nullptr;
static UINT                    g_resize_w          = 0;
static UINT                    g_resize_h          = 0;

static bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 1, D3D11_SDK_VERSION,
        &sd, &g_swap_chain, &g_device, &feature_level, &g_device_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* back_buf = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buf));
    g_device->CreateRenderTargetView(back_buf, nullptr, &g_main_rtv);
    back_buf->Release();
    return true;
}

static void cleanup_device() {
    if (g_main_rtv)  { g_main_rtv->Release();  g_main_rtv  = nullptr; }
    if (g_swap_chain){ g_swap_chain->Release(); g_swap_chain= nullptr; }
    if (g_device_ctx){ g_device_ctx->Release(); g_device_ctx= nullptr; }
    if (g_device)    { g_device->Release();     g_device    = nullptr; }
}

static void create_rtv() {
    ID3D11Texture2D* back_buf = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buf));
    g_device->CreateRenderTargetView(back_buf, nullptr, &g_main_rtv);
    back_buf->Release();
}

// ─── WndProc ──────────────────────────────────────────────────────────────────

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return TRUE;

    switch (msg) {
    case WM_SIZE:
        if (g_device && wp != SIZE_MINIMIZED) {
            g_resize_w = LOWORD(lp);
            g_resize_h = HIWORD(lp);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── WinMain ──────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NoriChat";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"NoriChat", L"NoriChat",
                                WS_OVERLAPPEDWINDOW,
                                100, 100, 1280, 720,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) { UnregisterClassW(wc.lpszClassName, hInstance); return 1; }

    if (!create_device(hwnd)) {
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ── ImGui setup ─────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr; // no imgui.ini

    ImGui::StyleColorsDark();
    // Slightly tweaked palette
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 4.f;
    style.FrameRounding    = 3.f;
    style.ScrollbarRounding = 3.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_device_ctx);

    // ── App objects ─────────────────────────────────────────────────────────
    AppState state;
    // http is rebuilt when the user edits host/port on the login screen.
    std::unique_ptr<HttpClient> http = std::make_unique<HttpClient>(
        state.server_host, state.server_port);
    WsClient   ws;
    LoginScreen login_screen;
    MainScreen  main_screen;

    const ImVec4 clear_color(0.06f, 0.07f, 0.08f, 1.f);
    bool running = true;

    // ── Main loop ────────────────────────────────────────────────────────────
    while (running) {
        // Process Windows messages
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        // Handle resize
        if (g_resize_w != 0) {
            if (g_main_rtv) { g_main_rtv->Release(); g_main_rtv = nullptr; }
            g_swap_chain->ResizeBuffers(0, g_resize_w, g_resize_h,
                                        DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            create_rtv();
        }

        // Re-create HttpClient if server address changed (simple approach)
        // (In production you'd do this only on address change)

        // ImGui new frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Fullscreen dockspace backdrop
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
        case AppState::Screen::Login: {
            // Rebuild HttpClient if the user edits host/port fields
            static char last_host[128] = {};
            static int  last_port      = 0;
            if (strcmp(last_host, state.server_host) != 0 ||
                last_port != state.server_port) {
                strncpy(last_host, state.server_host, sizeof(last_host));
                last_port = state.server_port;
                http = std::make_unique<HttpClient>(state.server_host,
                                                    state.server_port);
            }
            login_screen.render(state, *http, ws);
            break;
        }
        case AppState::Screen::Main:
            main_screen.update(state, *http, ws);
            break;
        }

        // Render
        ImGui::Render();
        g_device_ctx->OMSetRenderTargets(1, &g_main_rtv, nullptr);
        const float cc[4] = { clear_color.x, clear_color.y,
                               clear_color.z, clear_color.w };
        g_device_ctx->ClearRenderTargetView(g_main_rtv, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap_chain->Present(1, 0); // VSync on
    }

    // ── Cleanup ──────────────────────────────────────────────────────────────
    ws.disconnect();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanup_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}
