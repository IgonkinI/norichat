cmake_minimum_required(VERSION 3.16)
project(norichat_server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─── Build mode ───────────────────────────────────────────────────────────────
# NORICHAT_FETCH_LIBS=OFF  (default) – use system apt packages, low RAM usage.
# NORICHAT_FETCH_LIBS=ON            – download & build from source (needs ~1 GB RAM).
option(NORICHAT_FETCH_LIBS
       "Download and build libwebsockets + SQLite3 from source via FetchContent"
       OFF)

# ─── OpenSSL (always from system – tiny, header-only usage) ──────────────────
find_package(OpenSSL REQUIRED)

# ─── nlohmann/json – header-only, negligible compile cost ────────────────────
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Heavy deps: system (default) or FetchContent ────────────────────────────
if (NOT NORICHAT_FETCH_LIBS)
    # ── System mode ───────────────────────────────────────────────────────────
    # Install with:
    #   sudo apt install libsqlite3-dev libwebsockets-dev
    find_package(SQLite3 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)

    # Unified alias targets used below
    set(SQLITE_TARGET  SQLite::SQLite3)
    set(LWS_TARGET     PkgConfig::LWS)
    # No extra include dir needed; SQLite3 and LWS targets carry their own.
    set(SQLITE_INCLUDE "")

else()
    # ── FetchContent mode ─────────────────────────────────────────────────────
    # Tip: if OOM occurs, build with:  cmake --build build -j1
    # and set SQLite compile flags to -O0 to reduce peak RAM.

    FetchContent_Declare(
        sqlite_src
        URL      https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

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

    FetchContent_MakeAvailable(sqlite_src libwebsockets)

    add_library(sqlite3_lib STATIC ${sqlite_src_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_lib PUBLIC ${sqlite_src_SOURCE_DIR})
    target_compile_definitions(sqlite3_lib PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    )
    # -O0 drastically cuts peak RAM during sqlite3.c compilation
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sqlite3_lib PRIVATE -w -O0)
    endif()

    set(SQLITE_TARGET  sqlite3_lib)
    set(LWS_TARGET     websockets_static)
    set(SQLITE_INCLUDE ${sqlite_src_SOURCE_DIR})
endif()

# ─── Include path for shared protocol ────────────────────────────────────────
set(SHARED_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

# ─── Server executable ────────────────────────────────────────────────────────
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
    ${SQLITE_INCLUDE}   # empty when using system SQLite3
)

target_link_libraries(norichat_server PRIVATE
    ${SQLITE_TARGET}
    nlohmann_json::nlohmann_json
    ${LWS_TARGET}
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
