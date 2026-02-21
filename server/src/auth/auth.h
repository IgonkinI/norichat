#pragma once
#include <string>
#include <optional>

namespace auth {

// Returns "salt:sha256hex" suitable for storage.
std::string hash_password(const std::string& password);

// Checks password against the stored "salt:sha256hex" string.
bool verify_password(const std::string& password, const std::string& stored);

// Generates a signed HS256 JWT containing user_id and username.
std::string generate_jwt(int user_id, const std::string& username);

// Validates JWT signature and expiry.
// Returns user_id on success, nullopt on any failure.
std::optional<int> validate_jwt(const std::string& token);

// Extract the "Bearer <token>" part from an Authorization header value.
// Returns the raw token string, or empty if malformed.
std::string bearer_token(const std::string& header);

// Set the JWT signing secret at startup (before any tokens are issued).
void set_secret(std::string secret);

} // namespace auth
