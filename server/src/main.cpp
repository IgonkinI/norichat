#include "api/api.h"
#include "ws/ws.h"
#include "db/db.h"

#include <libwebsockets.h>
#include <cstdio>
#include <cstring>
#include <csignal>

// ─── Globals ──────────────────────────────────────────────────────────────────

static volatile int g_interrupted = 0;

static void sigint_handler(int) { g_interrupted = 1; }

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const char* db_path  = "norichat.db";
    int         port     = 8080;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc)     db_path = argv[++i];
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)   port    = atoi(argv[++i]);
    }

    // ── Database ──────────────────────────────────────────────────────────────
    if (!db::init(db_path)) {
        fprintf(stderr, "[main] failed to open database at %s\n", db_path);
        return 1;
    }
    fprintf(stdout, "[main] database opened: %s\n", db_path);

    // ── lws protocols ─────────────────────────────────────────────────────────
    // HTTP must be first; the WS protocol is matched by protocol name in the
    // Upgrade handshake.
    static lws_protocols protocols[] = {
        api::protocol,
        ws::protocol,
        { nullptr, nullptr, 0, 0 }
    };

    // ── lws context ───────────────────────────────────────────────────────────
    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = port;
    info.protocols = protocols;
    info.options   = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    // Disable built-in SSL (Phase 1 uses plain ws://)
    info.ssl_cert_filepath        = nullptr;
    info.ssl_private_key_filepath = nullptr;

    // Suppress lws noise in production; set LLL_NOTICE | LLL_WARN | LLL_ERR
    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    lws_context* ctx = lws_create_context(&info);
    if (!ctx) {
        fprintf(stderr, "[main] failed to create lws context\n");
        db::close();
        return 1;
    }

    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    fprintf(stdout,
            "[main] NoriChat server listening on port %d\n"
            "[main] Press Ctrl+C to stop.\n", port);

    // ── Event loop ────────────────────────────────────────────────────────────
    while (!g_interrupted) {
        int rc = lws_service(ctx, 100); // timeout_ms = 100
        if (rc < 0) break;
    }

    fprintf(stdout, "\n[main] shutting down\n");
    lws_context_destroy(ctx);
    db::close();
    return 0;
}
