#pragma once
#include <libwebsockets.h>
#include "../../../shared/protocol/messages.h"

namespace api {

// Per-connection session data for the HTTP protocol.
struct HttpSession {
    char uri[512];
    char body[HTTP_BODY_MAX];
    int  body_len;
    char auth_header[1024]; // "Bearer <token>"
    bool is_post;
};

// lws protocol entry – must be first in the protocols[] array.
extern lws_protocols protocol;

} // namespace api
