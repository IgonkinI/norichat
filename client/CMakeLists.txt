cmake_minimum_required(VERSION 3.16)
project(norichat_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static CRT on Windows to match vcpkg x64-windows-static triplet.
# Must be set before any target is defined (including FetchContent subdirs).
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ─── System / vcpkg deps (SDL2, libcurl, OpenGL) ─────────────────────────────
# Linux : sudo apt install libsdl2-dev libcurl4-openssl-dev libgl1-mesa-dev
# Windows: vcpkg install sdl2:x64-windows-static curl:x64-windows-static
#          cmake … -DCMAKE_TOOLCHAIN_FILE=$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake
#                  -DVCPKG_TARGET_TRIPLET=x64-windows-static
find_package(SDL2   REQUIRED)
find_package(CURL   REQUIRED)
find_package(OpenGL REQUIRED)

# Normalize SDL2 target – modern configs export SDL2::SDL2; older ones set
# SDL2_LIBRARIES / SDL2_INCLUDE_DIRS only.
if (NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED GLOBAL)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES      "${SDL2_LIBRARIES}")
endif()

# ─── nlohmann/json (header-only, tiny compile cost) ──────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Dear ImGui (no upstream CMakeLists – build sources manually) ─────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
    GIT_SHALLOW    TRUE
)
# FetchContent_Populate only: we handle add_library ourselves.
FetchContent_GetProperties(imgui)
if (NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# imgui_impl_sdl2 needs SDL2 headers; pull them in transitively.
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2)
if (MSVC)
    target_compile_options(imgui_lib PRIVATE /W0)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imgui_lib PRIVATE -w)
endif()

# ─── libwebsockets – static, no TLS (always FetchContent) ────────────────────
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
set(LWS_WITH_LIBEVENT       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libwebsockets
    GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
    GIT_TAG        v4.3.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libwebsockets)

# Suppress warnings in libwebsockets v4.3.3: client-http.c has an unused label
# 'hs2' that trips -Werror on GCC/Clang and C2220 on MSVC.
if (TARGET websockets_static)
    if (MSVC)
        target_compile_options(websockets_static PRIVATE /W0)
    elseif (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(websockets_static PRIVATE -w)
    endif()
endif()

# ─── Shared protocol include ──────────────────────────────────────────────────
set(SHARED_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

# ─── Client executable ────────────────────────────────────────────────────────
set(CLIENT_SOURCES
    src/main.cpp
    src/net/http_client.cpp
    src/net/ws_client.cpp
    src/ui/login_screen.cpp
    src/ui/main_screen.cpp
)

add_executable(norichat_client ${CLIENT_SOURCES})

target_include_directories(norichat_client PRIVATE
    src/
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(norichat_client PRIVATE
    imgui_lib
    nlohmann_json::nlohmann_json
    websockets_static
    CURL::libcurl
    OpenGL::GL
)

# ─── Windows specifics ────────────────────────────────────────────────────────
if (WIN32)
    # SDL2main provides WinMain → main() bridge; must come before SDL2.
    if (TARGET SDL2::SDL2main)
        target_link_libraries(norichat_client PRIVATE SDL2::SDL2main)
    endif()
    # Hide the console window in release.
    set_target_properties(norichat_client PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(norichat_client PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ─── Compiler warnings ────────────────────────────────────────────────────────
if (MSVC)
    target_compile_options(norichat_client PRIVATE /W3 /MP /utf-8)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(norichat_client PRIVATE -Wall -Wextra)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS norichat_client RUNTIME DESTINATION bin)
