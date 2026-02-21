#include "db.h"
#include "../../../shared/protocol/messages.h"

#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <ctime>

// ─── Globals ──────────────────────────────────────────────────────────────────

static sqlite3* g_db = nullptr;

// ─── Schema ───────────────────────────────────────────────────────────────────

static const char* SCHEMA = R"sql(
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    UNIQUE NOT NULL,
    password_hash TEXT    NOT NULL,
    created_at    INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS servers (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT    NOT NULL,
    owner_id INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS channels (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL REFERENCES servers(id),
    name      TEXT    NOT NULL,
    type      TEXT    NOT NULL DEFAULT 'text'
);

CREATE TABLE IF NOT EXISTS messages (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    channel_id INTEGER NOT NULL REFERENCES channels(id),
    author_id  INTEGER NOT NULL REFERENCES users(id),
    content    TEXT    NOT NULL,
    ts         INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS memberships (
    user_id   INTEGER NOT NULL REFERENCES users(id),
    server_id INTEGER NOT NULL REFERENCES servers(id),
    PRIMARY KEY (user_id, server_id)
);
)sql";

// ─── Helpers ──────────────────────────────────────────────────────────────────

static bool exec(const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] exec error: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

static sqlite3_stmt* prepare(const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[db] prepare error: %s  sql=%s\n",
                sqlite3_errmsg(g_db), sql);
        return nullptr;
    }
    return stmt;
}

// ─── Init / Close ─────────────────────────────────────────────────────────────

bool db::init(const char* path) {
    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "[db] cannot open %s: %s\n", path, sqlite3_errmsg(g_db));
        return false;
    }
    if (!exec(SCHEMA)) return false;

    // Seed: create default server + channel if not present
    {
        sqlite3_stmt* st = prepare("SELECT COUNT(*) FROM servers WHERE id=1");
        if (!st) return false;
        int count = 0;
        if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);

        if (count == 0) {
            exec("INSERT INTO servers(name,owner_id) VALUES('" DEFAULT_SERVER_NAME "',0)");
            exec("INSERT INTO channels(server_id,name,type) VALUES(1,'" DEFAULT_CHANNEL_NAME "','text')");
            fprintf(stdout, "[db] seeded default server and channel\n");
        }
    }
    return true;
}

void db::close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

// ─── Users ────────────────────────────────────────────────────────────────────

std::optional<User> db::create_user(const std::string& username,
                                    const std::string& password_hash) {
    sqlite3_stmt* st = prepare(
        "INSERT INTO users(username,password_hash,created_at) VALUES(?,?,?) "
        "RETURNING id,username,password_hash,created_at");
    if (!st) return std::nullopt;

    sqlite3_bind_text(st, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (int64_t)time(nullptr));

    std::optional<User> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        User u;
        u.id            = sqlite3_column_int(st, 0);
        u.username      = (const char*)sqlite3_column_text(st, 1);
        u.password_hash = (const char*)sqlite3_column_text(st, 2);
        u.created_at    = sqlite3_column_int64(st, 3);
        result = u;
    } else {
        fprintf(stderr, "[db] create_user: %s\n", sqlite3_errmsg(g_db));
    }
    sqlite3_finalize(st);
    return result;
}

std::optional<User> db::find_user_by_username(const std::string& username) {
    sqlite3_stmt* st = prepare(
        "SELECT id,username,password_hash,created_at FROM users WHERE username=?");
    if (!st) return std::nullopt;

    sqlite3_bind_text(st, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<User> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        User u;
        u.id            = sqlite3_column_int(st, 0);
        u.username      = (const char*)sqlite3_column_text(st, 1);
        u.password_hash = (const char*)sqlite3_column_text(st, 2);
        u.created_at    = sqlite3_column_int64(st, 3);
        result = u;
    }
    sqlite3_finalize(st);
    return result;
}

std::optional<User> db::find_user_by_id(int id) {
    sqlite3_stmt* st = prepare(
        "SELECT id,username,password_hash,created_at FROM users WHERE id=?");
    if (!st) return std::nullopt;

    sqlite3_bind_int(st, 1, id);

    std::optional<User> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        User u;
        u.id            = sqlite3_column_int(st, 0);
        u.username      = (const char*)sqlite3_column_text(st, 1);
        u.password_hash = (const char*)sqlite3_column_text(st, 2);
        u.created_at    = sqlite3_column_int64(st, 3);
        result = u;
    }
    sqlite3_finalize(st);
    return result;
}

// ─── Servers ──────────────────────────────────────────────────────────────────

std::optional<Server> db::create_server(const std::string& name, int owner_id) {
    sqlite3_stmt* st = prepare(
        "INSERT INTO servers(name,owner_id) VALUES(?,?) "
        "RETURNING id,name,owner_id");
    if (!st) return std::nullopt;

    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, owner_id);

    std::optional<Server> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        Server s;
        s.id       = sqlite3_column_int(st, 0);
        s.name     = (const char*)sqlite3_column_text(st, 1);
        s.owner_id = sqlite3_column_int(st, 2);
        result = s;
    }
    sqlite3_finalize(st);
    return result;
}

std::vector<Server> db::get_user_servers(int user_id) {
    std::vector<Server> servers;
    sqlite3_stmt* st = prepare(
        "SELECT s.id,s.name,s.owner_id FROM servers s "
        "JOIN memberships m ON m.server_id=s.id "
        "WHERE m.user_id=? ORDER BY s.id");
    if (!st) return servers;

    sqlite3_bind_int(st, 1, user_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Server s;
        s.id       = sqlite3_column_int(st, 0);
        s.name     = (const char*)sqlite3_column_text(st, 1);
        s.owner_id = sqlite3_column_int(st, 2);
        servers.push_back(s);
    }
    sqlite3_finalize(st);
    return servers;
}

// ─── Channels ─────────────────────────────────────────────────────────────────

std::optional<Channel> db::create_channel(int server_id,
                                          const std::string& name,
                                          const std::string& type) {
    sqlite3_stmt* st = prepare(
        "INSERT INTO channels(server_id,name,type) VALUES(?,?,?) "
        "RETURNING id,server_id,name,type");
    if (!st) return std::nullopt;

    sqlite3_bind_int(st, 1, server_id);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, type.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Channel> result;
    if (sqlite3_step(st) == SQLITE_ROW) {
        Channel c;
        c.id        = sqlite3_column_int(st, 0);
        c.server_id = sqlite3_column_int(st, 1);
        c.name      = (const char*)sqlite3_column_text(st, 2);
        c.type      = (const char*)sqlite3_column_text(st, 3);
        result = c;
    }
    sqlite3_finalize(st);
    return result;
}

std::vector<Channel> db::get_server_channels(int server_id) {
    std::vector<Channel> channels;
    sqlite3_stmt* st = prepare(
        "SELECT id,server_id,name,type FROM channels WHERE server_id=? ORDER BY id");
    if (!st) return channels;

    sqlite3_bind_int(st, 1, server_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Channel c;
        c.id        = sqlite3_column_int(st, 0);
        c.server_id = sqlite3_column_int(st, 1);
        c.name      = (const char*)sqlite3_column_text(st, 2);
        c.type      = (const char*)sqlite3_column_text(st, 3);
        channels.push_back(c);
    }
    sqlite3_finalize(st);
    return channels;
}

// ─── Messages ─────────────────────────────────────────────────────────────────

int64_t db::add_message(int channel_id, int author_id, const std::string& content) {
    sqlite3_stmt* st = prepare(
        "INSERT INTO messages(channel_id,author_id,content,ts) VALUES(?,?,?,?) "
        "RETURNING id");
    if (!st) return -1;

    sqlite3_bind_int(st, 1, channel_id);
    sqlite3_bind_int(st, 2, author_id);
    sqlite3_bind_text(st, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, (int64_t)time(nullptr));

    int64_t id = -1;
    if (sqlite3_step(st) == SQLITE_ROW)
        id = sqlite3_column_int64(st, 0);
    else
        fprintf(stderr, "[db] add_message: %s\n", sqlite3_errmsg(g_db));

    sqlite3_finalize(st);
    return id;
}

std::vector<Message> db::get_messages(int channel_id, int limit) {
    std::vector<Message> msgs;
    sqlite3_stmt* st = prepare(
        "SELECT m.id,m.channel_id,m.author_id,u.username,m.content,m.ts "
        "FROM messages m JOIN users u ON u.id=m.author_id "
        "WHERE m.channel_id=? ORDER BY m.id DESC LIMIT ?");
    if (!st) return msgs;

    sqlite3_bind_int(st, 1, channel_id);
    sqlite3_bind_int(st, 2, limit);

    while (sqlite3_step(st) == SQLITE_ROW) {
        Message msg;
        msg.id          = sqlite3_column_int(st, 0);
        msg.channel_id  = sqlite3_column_int(st, 1);
        msg.author_id   = sqlite3_column_int(st, 2);
        msg.author_name = (const char*)sqlite3_column_text(st, 3);
        msg.content     = (const char*)sqlite3_column_text(st, 4);
        msg.ts          = sqlite3_column_int64(st, 5);
        msgs.push_back(msg);
    }
    sqlite3_finalize(st);

    // Return in chronological order
    std::reverse(msgs.begin(), msgs.end());
    return msgs;
}

// ─── Memberships ──────────────────────────────────────────────────────────────

bool db::add_membership(int user_id, int server_id) {
    sqlite3_stmt* st = prepare(
        "INSERT OR IGNORE INTO memberships(user_id,server_id) VALUES(?,?)");
    if (!st) return false;

    sqlite3_bind_int(st, 1, user_id);
    sqlite3_bind_int(st, 2, server_id);

    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

bool db::has_membership(int user_id, int server_id) {
    sqlite3_stmt* st = prepare(
        "SELECT 1 FROM memberships WHERE user_id=? AND server_id=?");
    if (!st) return false;

    sqlite3_bind_int(st, 1, user_id);
    sqlite3_bind_int(st, 2, server_id);

    bool found = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return found;
}
