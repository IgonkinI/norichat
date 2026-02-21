#include "auth.h"

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

// ─── Config ───────────────────────────────────────────────────────────────────

static std::string g_jwt_secret = "norichat_secret_CHANGE_ME_in_production";
static const int   JWT_TTL_SEC  = 86400 * 7; // 7 days

void auth::set_secret(std::string secret) {
    g_jwt_secret = std::move(secret);
}

// ─── Base64url ────────────────────────────────────────────────────────────────

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64url_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned char b0 = data[i];
        unsigned char b1 = (i + 1 < len) ? data[i + 1] : 0;
        unsigned char b2 = (i + 2 < len) ? data[i + 2] : 0;
        out += B64_CHARS[b0 >> 2];
        out += B64_CHARS[((b0 & 0x03) << 4) | (b1 >> 4)];
        out += (i + 1 < len) ? B64_CHARS[((b1 & 0x0f) << 2) | (b2 >> 6)] : '=';
        out += (i + 2 < len) ? B64_CHARS[b2 & 0x3f] : '=';
    }
    // url-safe + strip padding
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

static std::string b64url_encode(const std::string& s) {
    return b64url_encode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static std::string b64url_decode(const std::string& encoded) {
    // Re-pad
    std::string s = encoded;
    while (s.size() % 4 != 0) s += '=';

    std::string out;
    out.reserve(s.size() * 3 / 4);

    for (size_t i = 0; i + 3 < s.size(); i += 4) {
        int v0 = b64_val(s[i]);
        int v1 = b64_val(s[i + 1]);
        int v2 = b64_val(s[i + 2]);
        int v3 = b64_val(s[i + 3]);
        if (v0 < 0 || v1 < 0) break;
        out += (char)((v0 << 2) | (v1 >> 4));
        if (s[i + 2] != '=' && v2 >= 0)
            out += (char)(((v1 & 0x0f) << 4) | (v2 >> 2));
        if (s[i + 3] != '=' && v3 >= 0)
            out += (char)(((v2 & 0x03) << 6) | v3);
    }
    return out;
}

// ─── Hex helpers ──────────────────────────────────────────────────────────────

static std::string to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return oss.str();
}

// ─── HMAC-SHA256 ──────────────────────────────────────────────────────────────

static std::string hmac_sha256_b64url(const std::string& msg, const std::string& key) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  dlen = 0;
    HMAC(EVP_sha256(),
         key.data(), (int)key.size(),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
         digest, &dlen);
    return b64url_encode(digest, dlen);
}

// ─── SHA-256 ──────────────────────────────────────────────────────────────────

static std::string sha256_hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), digest);
    return to_hex(digest, SHA256_DIGEST_LENGTH);
}

// ─── Public API ───────────────────────────────────────────────────────────────

std::string auth::hash_password(const std::string& password) {
    unsigned char buf[8];
    RAND_bytes(buf, sizeof(buf));
    std::string salt = to_hex(buf, sizeof(buf)); // 16 hex chars
    return salt + ":" + sha256_hex(salt + password);
}

bool auth::verify_password(const std::string& password, const std::string& stored) {
    auto colon = stored.find(':');
    if (colon == std::string::npos) return false;
    const std::string salt          = stored.substr(0, colon);
    const std::string expected_hash = stored.substr(colon + 1);
    const std::string actual_hash   = sha256_hex(salt + password);

    // Constant-time compare
    if (actual_hash.size() != expected_hash.size()) return false;
    volatile int diff = 0;
    for (size_t i = 0; i < actual_hash.size(); i++)
        diff |= actual_hash[i] ^ expected_hash[i];
    return diff == 0;
}

std::string auth::generate_jwt(int user_id, const std::string& username) {
    // Header
    const std::string header_b64 = b64url_encode(R"({"alg":"HS256","typ":"JWT"})");

    // Payload
    nlohmann::json payload;
    payload["sub"]      = user_id;
    payload["username"] = username;
    payload["exp"]      = (int64_t)time(nullptr) + JWT_TTL_SEC;
    const std::string payload_b64 = b64url_encode(payload.dump());

    // Signature
    const std::string signing_input = header_b64 + "." + payload_b64;
    const std::string sig_b64       = hmac_sha256_b64url(signing_input, g_jwt_secret);

    return signing_input + "." + sig_b64;
}

std::optional<int> auth::validate_jwt(const std::string& token) {
    // Split into three parts
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    const std::string header_b64  = token.substr(0, dot1);
    const std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    const std::string sig_b64     = token.substr(dot2 + 1);

    // Verify signature
    const std::string signing_input  = header_b64 + "." + payload_b64;
    const std::string expected_sig   = hmac_sha256_b64url(signing_input, g_jwt_secret);
    if (sig_b64 != expected_sig) return std::nullopt;

    // Decode and parse payload
    std::string payload_json = b64url_decode(payload_b64);
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload_json);
    } catch (...) {
        return std::nullopt;
    }

    // Check expiry
    int64_t exp = j.value("exp", (int64_t)0);
    if (exp < (int64_t)time(nullptr)) return std::nullopt;

    int user_id = j.value("sub", 0);
    if (user_id <= 0) return std::nullopt;
    return user_id;
}

std::string auth::bearer_token(const std::string& header) {
    const std::string prefix = "Bearer ";
    if (header.size() <= prefix.size()) return "";
    if (header.substr(0, prefix.size()) != prefix) return "";
    return header.substr(prefix.size());
}
