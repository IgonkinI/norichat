cmake_minimum_required(VERSION 3.16)
project(norichat_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR "norichat_client requires Windows (DirectX 11 / WinHTTP).")
endif()

# ─── FetchContent dependencies ────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ── Dear ImGui (no CMakeLists.txt upstream, build manually) ──────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(imgui)   # Populate only; we add sources ourselves.

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# Silence MSVC warnings in third-party code
if (MSVC)
    target_compile_options(imgui PRIVATE /W0)
endif()

# ── nlohmann/json (shared with server) ───────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# ── libwebsockets – static, no TLS (Phase 1) ─────────────────────────────────
set(LWS_WITH_SSL            OFF CACHE BOOL "" FORCE)
set(LWS_WITH_SHARED         OFF CACHE BOOL "" FORCE)
set(LWS_WITH_STATIC         ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TESTAPPS    ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_SERVER ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_CLIENT ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_PING   ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_ECHO   ON  CACHE BOOL "" FORCE)
set(LWS_WITH_HTTP2          OFF CACHE BOOL "" FORCE)
set(LWS_WITH_ZLIB           OFF CACHE BOOL "" FORCE)
set(LWS_WITHOUT_EXTENSIONS  ON  CACHE BOOL "" FORCE)
set(LWS_WITH_LIBUV          OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libwebsockets
    GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
    GIT_TAG        v4.3.3
    GIT_SHALLOW    TRUE
)

# ── miniaudio (single-header, Phase 1 placeholder) ───────────────────────────
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(miniaudio)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})

FetchContent_MakeAvailable(nlohmann_json libwebsockets)

# ─── Shared protocol include ──────────────────────────────────────────────────
set(SHARED_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

# ─── Client sources ───────────────────────────────────────────────────────────
set(CLIENT_SOURCES
    src/main.cpp
    src/net/http_client.cpp
    src/net/ws_client.cpp
    src/ui/login_screen.cpp
    src/ui/main_screen.cpp
)

# WIN32 = no console window in release; linker entry is WinMain.
add_executable(norichat_client WIN32 ${CLIENT_SOURCES})

target_include_directories(norichat_client PRIVATE
    src/
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(norichat_client PRIVATE
    imgui
    nlohmann_json::nlohmann_json
    websockets_static
    miniaudio
    d3d11
    dxgi
    winhttp
)

# ─── Compiler options ─────────────────────────────────────────────────────────
if (MSVC)
    target_compile_options(norichat_client PRIVATE /W3 /MP /utf-8)
    # Avoid min/max macro conflicts
    target_compile_definitions(norichat_client PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
elseif (MINGW)
    target_compile_options(norichat_client PRIVATE -Wall -Wextra)
    target_compile_definitions(norichat_client PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_options(norichat_client PRIVATE -mwindows)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS norichat_client RUNTIME DESTINATION bin)
