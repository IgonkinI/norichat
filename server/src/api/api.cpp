#include "api.h"
#include "../auth/auth.h"
#include "../db/db.h"
#include "../../../shared/protocol/messages.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Parse "key=value" pairs from a query string or URI fragment.
static std::string query_param(const char* uri, const char* key) {
    const char* q = strchr(uri, '?');
    if (!q) return "";
    q++; // skip '?'

    std::string haystack = q;
    std::string search   = std::string(key) + "=";
    auto pos = haystack.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    auto end = haystack.find('&', pos);
    if (end == std::string::npos) return haystack.substr(pos);
    return haystack.substr(pos, end - pos);
}

// Strip query string from URI to get the plain path.
static std::string uri_path(const char* uri) {
    const char* q = strchr(uri, '?');
    if (!q) return uri;
    return std::string(uri, q);
}

// Write a JSON response and close the HTTP transaction.
static int send_json(lws* wsi, int status, const std::string& body) {
    const size_t blen = body.size();

    // Headers buffer
    std::vector<unsigned char> hdr(LWS_PRE + 1024);
    unsigned char* p   = hdr.data() + LWS_PRE;
    unsigned char* end = hdr.data() + hdr.size();

    if (lws_add_http_header_status(wsi, (unsigned int)status, &p, end))          return -1;
    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
            (unsigned char*)"application/json", 16, &p, end))                    return -1;
    if (lws_add_http_header_content_length(wsi, blen, &p, end))                  return -1;
    if (lws_add_http_header_by_name(wsi,
            (unsigned char*)"Access-Control-Allow-Origin:",
            (unsigned char*)"*", 1, &p, end))                                    return -1;
    if (lws_finalize_http_header(wsi, &p, end))                                  return -1;

    size_t hlen = (size_t)(p - (hdr.data() + LWS_PRE));
    lws_write(wsi, hdr.data() + LWS_PRE, hlen, LWS_WRITE_HTTP_HEADERS);

    // Body buffer
    std::vector<unsigned char> buf(LWS_PRE + blen);
    memcpy(buf.data() + LWS_PRE, body.data(), blen);
    lws_write(wsi, buf.data() + LWS_PRE, blen, LWS_WRITE_HTTP_FINAL);

    return lws_http_transaction_completed(wsi);
}

static int send_error_json(lws* wsi, int status, const std::string& msg) {
    json j;
    j["error"] = msg;
    return send_json(wsi, status, j.dump());
}

// ─── Route handlers ───────────────────────────────────────────────────────────

static int handle_register(lws* wsi, api::HttpSession* s) {
    json req;
    try { req = json::parse(std::string(s->body, s->body_len)); }
    catch (...) { return send_error_json(wsi, 400, "invalid JSON"); }

    std::string username = req.value("username", "");
    std::string password = req.value("password", "");
    if (username.empty() || password.empty())
        return send_error_json(wsi, 400, "username and password required");
    if (username.size() > 32 || password.size() > 128)
        return send_error_json(wsi, 400, "username or password too long");

    // Check duplicate
    if (db::find_user_by_username(username))
        return send_error_json(wsi, 409, "username already taken");

    auto user = db::create_user(username, auth::hash_password(password));
    if (!user)
        return send_error_json(wsi, 500, "failed to create user");

    // Auto-join default server (id=1)
    db::add_membership(user->id, 1);

    json resp;
    resp["token"]    = auth::generate_jwt(user->id, user->username);
    resp["user_id"]  = user->id;
    resp["username"] = user->username;
    return send_json(wsi, 201, resp.dump());
}

static int handle_login(lws* wsi, api::HttpSession* s) {
    json req;
    try { req = json::parse(std::string(s->body, s->body_len)); }
    catch (...) { return send_error_json(wsi, 400, "invalid JSON"); }

    std::string username = req.value("username", "");
    std::string password = req.value("password", "");
    if (username.empty() || password.empty())
        return send_error_json(wsi, 400, "username and password required");

    auto user = db::find_user_by_username(username);
    if (!user || !auth::verify_password(password, user->password_hash))
        return send_error_json(wsi, 401, "invalid credentials");

    // Ensure membership in default server (idempotent – INSERT OR IGNORE)
    db::add_membership(user->id, 1);

    json resp;
    resp["token"]    = auth::generate_jwt(user->id, user->username);
    resp["user_id"]  = user->id;
    resp["username"] = user->username;
    return send_json(wsi, 200, resp.dump());
}

static int handle_get_servers(lws* wsi, api::HttpSession* s) {
    std::string token = auth::bearer_token(s->auth_header);
    auto uid = auth::validate_jwt(token);
    if (!uid) return send_error_json(wsi, 401, "unauthorized");

    auto servers = db::get_user_servers(*uid);
    json arr = json::array();
    for (auto& sv : servers) {
        json o;
        o["id"]       = sv.id;
        o["name"]     = sv.name;
        o["owner_id"] = sv.owner_id;
        arr.push_back(o);
    }
    return send_json(wsi, 200, arr.dump());
}

static int handle_get_channels(lws* wsi, api::HttpSession* s) {
    std::string token = auth::bearer_token(s->auth_header);
    auto uid = auth::validate_jwt(token);
    if (!uid) return send_error_json(wsi, 401, "unauthorized");

    std::string sid_str = query_param(s->uri, "server_id");
    if (sid_str.empty()) return send_error_json(wsi, 400, "server_id required");
    int server_id = std::stoi(sid_str);

    if (!db::has_membership(*uid, server_id))
        return send_error_json(wsi, 403, "not a member of this server");

    auto channels = db::get_server_channels(server_id);
    json arr = json::array();
    for (auto& ch : channels) {
        json o;
        o["id"]        = ch.id;
        o["server_id"] = ch.server_id;
        o["name"]      = ch.name;
        o["type"]      = ch.type;
        arr.push_back(o);
    }
    return send_json(wsi, 200, arr.dump());
}

static int handle_get_members(lws* wsi, api::HttpSession* s) {
    std::string token = auth::bearer_token(s->auth_header);
    auto uid = auth::validate_jwt(token);
    if (!uid) return send_error_json(wsi, 401, "unauthorized");

    std::string sid_str = query_param(s->uri, "server_id");
    if (sid_str.empty()) return send_error_json(wsi, 400, "server_id required");
    int server_id = std::stoi(sid_str);

    if (!db::has_membership(*uid, server_id))
        return send_error_json(wsi, 403, "not a member of this server");

    auto members = db::get_server_members(server_id);
    json arr = json::array();
    for (auto& m : members) {
        json o;
        o["id"]       = m.id;
        o["username"] = m.username;
        arr.push_back(o);
    }
    return send_json(wsi, 200, arr.dump());
}

static int handle_get_messages(lws* wsi, api::HttpSession* s) {
    std::string token = auth::bearer_token(s->auth_header);
    auto uid = auth::validate_jwt(token);
    if (!uid) return send_error_json(wsi, 401, "unauthorized");

    std::string cid_str = query_param(s->uri, "channel_id");
    if (cid_str.empty()) return send_error_json(wsi, 400, "channel_id required");
    int channel_id = std::stoi(cid_str);

    std::string lim_str = query_param(s->uri, "limit");
    int limit = lim_str.empty() ? DEFAULT_MSG_LIMIT : std::stoi(lim_str);
    if (limit <= 0 || limit > 200) limit = DEFAULT_MSG_LIMIT;

    auto msgs = db::get_messages(channel_id, limit);
    json arr = json::array();
    for (auto& m : msgs) {
        json o;
        o["id"]         = m.id;
        o["channel_id"] = m.channel_id;
        o["author"]     = m.author_name;
        o["content"]    = m.content;
        o["ts"]         = m.ts;
        arr.push_back(o);
    }
    return send_json(wsi, 200, arr.dump());
}

// ─── Dispatch ─────────────────────────────────────────────────────────────────

static int dispatch_get(lws* wsi, api::HttpSession* s) {
    std::string path = uri_path(s->uri);
    if (path == API_SERVERS)  return handle_get_servers(wsi, s);
    if (path == API_CHANNELS) return handle_get_channels(wsi, s);
    if (path == API_MESSAGES) return handle_get_messages(wsi, s);
    if (path == API_MEMBERS)  return handle_get_members(wsi, s);
    return send_error_json(wsi, 404, "not found");
}

static int dispatch_post(lws* wsi, api::HttpSession* s) {
    std::string path = uri_path(s->uri);
    if (path == API_REGISTER) return handle_register(wsi, s);
    if (path == API_LOGIN)    return handle_login(wsi, s);
    return send_error_json(wsi, 404, "not found");
}

// ─── lws callback ─────────────────────────────────────────────────────────────

static int http_callback(lws* wsi, lws_callback_reasons reason,
                         void* user, void* in, size_t len) {
    auto* s = static_cast<api::HttpSession*>(user);

    switch (reason) {
    // ── Request headers received ────────────────────────────────────────────
    case LWS_CALLBACK_HTTP: {
        if (!in) return -1;
        memset(s, 0, sizeof(*s));

        // Copy URI (may contain query string)
        strncpy(s->uri, static_cast<char*>(in), sizeof(s->uri) - 1);

        // Grab Authorization header
        lws_hdr_copy(wsi, s->auth_header, sizeof(s->auth_header),
                     WSI_TOKEN_HTTP_AUTHORIZATION);

        // Detect method: POST_URI token is set for POST requests in HTTP/1.1
        s->is_post = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COLON_METHOD) > 0
            ? [&]{
                char meth[16] = {};
                lws_hdr_copy(wsi, meth, sizeof(meth), WSI_TOKEN_HTTP_COLON_METHOD);
                return strcmp(meth, "POST") == 0;
              }()
            : (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0);

        if (!s->is_post) {
            // GET — handle immediately
            return dispatch_get(wsi, s);
        }
        // POST — wait for body chunks
        return 0;
    }

    // ── Body chunk received ──────────────────────────────────────────────────
    case LWS_CALLBACK_HTTP_BODY: {
        int remaining = (int)(sizeof(s->body) - 1 - s->body_len);
        if (remaining > 0) {
            int copy = (int)len < remaining ? (int)len : remaining;
            memcpy(s->body + s->body_len, in, copy);
            s->body_len += copy;
        }
        return 0;
    }

    // ── All body received ────────────────────────────────────────────────────
    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
        return dispatch_post(wsi, s);

    // ── CORS preflight ───────────────────────────────────────────────────────
    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
        return 0; // let lws handle WebSocket upgrades

    default:
        break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

// ─── Protocol descriptor ──────────────────────────────────────────────────────

lws_protocols api::protocol = {
    "http",
    http_callback,
    sizeof(api::HttpSession),
    0,    // rx_buffer_size (auto for HTTP)
    0, nullptr, 0
};
