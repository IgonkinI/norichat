#pragma once

// ─── WebSocket opcodes ────────────────────────────────────────────────────────
// Client → Server
#define OP_AUTH             "AUTH"
#define OP_CHANNEL_JOIN     "CHANNEL_JOIN"
#define OP_CHANNEL_LEAVE    "CHANNEL_LEAVE"
#define OP_MESSAGE_SEND     "MESSAGE_SEND"
#define OP_MESSAGE_EDIT     "MESSAGE_EDIT"
#define OP_MESSAGE_DELETE   "MESSAGE_DELETE"
// Voice – Client → Server
#define OP_VOICE_JOIN       "VOICE_JOIN"
#define OP_VOICE_LEAVE      "VOICE_LEAVE"
#define OP_VOICE_DATA       "VOICE_DATA"    // {channel_id, data:<base64 PCM>}

// Server → Client
#define OP_AUTH_OK          "AUTH_OK"
#define OP_AUTH_FAIL        "AUTH_FAIL"
#define OP_MESSAGE_NEW      "MESSAGE_NEW"
#define OP_USER_ONLINE      "USER_ONLINE"
#define OP_USER_OFFLINE     "USER_OFFLINE"
#define OP_MESSAGE_EDITED   "MESSAGE_EDITED"
#define OP_MESSAGE_DELETED  "MESSAGE_DELETED"
#define OP_ERROR            "ERROR"
// Voice – Server → Client
#define OP_VOICE_JOIN_OK    "VOICE_JOIN_OK"  // {channel_id, participants:[{user_id,username}]}
#define OP_VOICE_JOINED     "VOICE_JOINED"   // {channel_id, user_id, username}
#define OP_VOICE_LEFT       "VOICE_LEFT"     // {channel_id, user_id}

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
