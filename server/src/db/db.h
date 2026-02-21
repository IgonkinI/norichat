#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// ─── Domain types ─────────────────────────────────────────────────────────────

struct User {
    int         id          = 0;
    std::string username;
    std::string password_hash;
    int64_t     created_at  = 0;
};

struct Server {
    int         id          = 0;
    std::string name;
    int         owner_id    = 0;
};

struct Channel {
    int         id          = 0;
    int         server_id   = 0;
    std::string name;
    std::string type;       // "text" | "voice"
};

struct Message {
    int         id          = 0;
    int         channel_id  = 0;
    int         author_id   = 0;
    std::string author_name;
    std::string content;
    int64_t     ts          = 0;
};

struct Member {
    int         id       = 0;
    std::string username;
};

// ─── DB API ───────────────────────────────────────────────────────────────────
namespace db {

// Open/create the database at `path`, run schema migrations.
// Seeds a default server + channel on first run.
// Returns false on failure.
bool init(const char* path);
void close();

// Users
std::optional<User> create_user(const std::string& username,
                                const std::string& password_hash);
std::optional<User> find_user_by_username(const std::string& username);
std::optional<User> find_user_by_id(int id);

// Servers
std::optional<Server> create_server(const std::string& name, int owner_id);
std::vector<Server>   get_user_servers(int user_id);

// Channels
std::optional<Channel> create_channel(int server_id,
                                      const std::string& name,
                                      const std::string& type = "text");
std::vector<Channel> get_server_channels(int server_id);

// Messages
// Returns the new message id, or -1 on error.
int64_t add_message(int channel_id, int author_id, const std::string& content);
std::vector<Message> get_messages(int channel_id, int limit);

// Memberships
bool add_membership(int user_id, int server_id);
bool has_membership(int user_id, int server_id);
std::vector<Member> get_server_members(int server_id);

} // namespace db
