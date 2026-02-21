#pragma once

// ─── WebSocket opcodes ────────────────────────────────────────────────────────
// Client → Server
#define OP_AUTH           "AUTH"
#define OP_CHANNEL_JOIN   "CHANNEL_JOIN"
#define OP_CHANNEL_LEAVE  "CHANNEL_LEAVE"
#define OP_MESSAGE_SEND   "MESSAGE_SEND"

// Server → Client
#define OP_AUTH_OK        "AUTH_OK"
#define OP_AUTH_FAIL      "AUTH_FAIL"
#define OP_MESSAGE_NEW    "MESSAGE_NEW"
#define OP_USER_ONLINE    "USER_ONLINE"
#define OP_USER_OFFLINE   "USER_OFFLINE"
#define OP_ERROR          "ERROR"

// ─── HTTP paths ───────────────────────────────────────────────────────────────
#define API_REGISTER      "/api/register"
#define API_LOGIN         "/api/login"
#define API_SERVERS       "/api/servers"
#define API_CHANNELS      "/api/channels"
#define API_MESSAGES      "/api/messages"
#define API_MEMBERS       "/api/members"

// ─── Limits ───────────────────────────────────────────────────────────────────
#define MAX_MSG_LEN       4000
#define DEFAULT_MSG_LIMIT 50
#define WS_RX_BUFFER      65536
#define HTTP_BODY_MAX     8192

// ─── Default data ─────────────────────────────────────────────────────────────
#define DEFAULT_SERVER_NAME "NoriChat HQ"
#define DEFAULT_CHANNEL_NAME "general"
