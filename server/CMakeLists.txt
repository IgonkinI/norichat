cmake_minimum_required(VERSION 3.16)
project(norichat_server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─── OpenSSL (for HMAC-SHA256 / password hashing) ────────────────────────────
find_package(OpenSSL REQUIRED)

# ─── FetchContent dependencies ────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# SQLite3 amalgamation (single .c + .h)
FetchContent_Declare(
    sqlite_src
    URL      https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# libwebsockets – static, no TLS (Phase 1 uses ws://)
set(LWS_WITH_SSL             OFF CACHE BOOL "" FORCE)
set(LWS_WITH_SHARED          OFF CACHE BOOL "" FORCE)
set(LWS_WITH_STATIC          ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TESTAPPS     ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_SERVER  ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_CLIENT  ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_PING    ON  CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_ECHO    ON  CACHE BOOL "" FORCE)
set(LWS_WITH_HTTP2           OFF CACHE BOOL "" FORCE)
set(LWS_WITH_ZLIB            OFF CACHE BOOL "" FORCE)
set(LWS_WITHOUT_EXTENSIONS   ON  CACHE BOOL "" FORCE)
set(LWS_WITH_LIBUV           OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LIBEVENT        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libwebsockets
    GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
    GIT_TAG        v4.3.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json sqlite_src libwebsockets)

# ─── SQLite3 static library ───────────────────────────────────────────────────
add_library(sqlite3_lib STATIC ${sqlite_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite_src_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
)
# Suppress warnings in third-party code
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sqlite3_lib PRIVATE -w)
endif()

# ─── Include path for shared protocol ────────────────────────────────────────
set(SHARED_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

# ─── Server sources ───────────────────────────────────────────────────────────
set(SERVER_SOURCES
    src/main.cpp
    src/db/db.cpp
    src/auth/auth.cpp
    src/api/api.cpp
    src/ws/ws.cpp
)

add_executable(norichat_server ${SERVER_SOURCES})

target_include_directories(norichat_server PRIVATE
    src/
    ${SHARED_INCLUDE_DIR}
    ${sqlite_src_SOURCE_DIR}
)

target_link_libraries(norichat_server PRIVATE
    sqlite3_lib
    nlohmann_json::nlohmann_json
    websockets_static         # target created by libwebsockets FetchContent
    OpenSSL::Crypto
)

# ─── Compiler hardening ───────────────────────────────────────────────────────
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(norichat_server PRIVATE
        -Wall -Wextra -Wpedantic
        -fstack-protector-strong
    )
    target_link_options(norichat_server PRIVATE
        -Wl,-z,relro -Wl,-z,now
    )
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS norichat_server RUNTIME DESTINATION bin)
