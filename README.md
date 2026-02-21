# NoriChat

Lightweight open-source messenger — Phase 1 (text chat over WebSocket).

```
Server: Linux / C++17 / libwebsockets · SQLite3 · OpenSSL
Client: Windows / C++17 / ImGui+DX11 · libwebsockets · WinHTTP
```

---

## Directory structure

```
norichat/
├── server/              # Linux server binary
│   ├── src/
│   │   ├── main.cpp
│   │   ├── api/         # HTTP REST handlers (libwebsockets HTTP)
│   │   ├── ws/          # WebSocket session manager
│   │   ├── db/          # SQLite3 layer
│   │   └── auth/        # SHA-256 password hash + HS256 JWT
│   └── CMakeLists.txt
├── client/              # Windows GUI client
│   ├── src/
│   │   ├── main.cpp     # Win32 window · D3D11 · ImGui loop
│   │   ├── net/         # HttpClient (WinHTTP) + WsClient (libwebsockets)
│   │   └── ui/          # LoginScreen · MainScreen
│   └── CMakeLists.txt
├── shared/
│   └── protocol/messages.h   # opcode/path constants shared by both
└── docker/Dockerfile
```

---

## Building the server (Linux)

### Prerequisites

```bash
sudo apt install build-essential cmake git libssl-dev pkg-config
```

### Build

```bash
cd norichat/server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Binary is at `build/norichat_server`.

### Run

```bash
./build/norichat_server                        # defaults: port 8080, db=norichat.db
./build/norichat_server --port 9000 --db /data/chat.db
```

On first run, a default server **"NoriChat HQ"** and channel **"general"** are created automatically. Every registered user is joined to this server.

---

## Building the client (Windows)

### Prerequisites

- Visual Studio 2022 (with *Desktop development with C++* workload) **or** MinGW-w64 + CMake
- Windows SDK 10.0+ (for D3D11 / WinHTTP — included in VS)
- CMake ≥ 3.16
- Git

### Build with Visual Studio

```powershell
cd norichat\client
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Build with MinGW

```bash
cd norichat/client
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Binary is at `build/norichat_client.exe` (or `build/Release/norichat_client.exe` for MSVC).

---

## Running with Docker (server)

```bash
# Build image (run from the norichat/ root)
docker build -f docker/Dockerfile -t norichat-server .

# Run (persist database on host)
docker run -d -p 8080:8080 -v norichat_data:/data --name norichat norichat-server
```

---

## REST API

| Method | Path | Auth | Body / Query | Response |
|--------|------|------|--------------|----------|
| POST | `/api/register` | – | `{username, password}` | `{token, user_id, username}` |
| POST | `/api/login` | – | `{username, password}` | `{token, user_id, username}` |
| GET | `/api/servers` | Bearer | – | `[{id, name, owner_id}]` |
| GET | `/api/channels?server_id=X` | Bearer | – | `[{id, server_id, name, type}]` |
| GET | `/api/messages?channel_id=X&limit=50` | Bearer | – | `[{id, channel_id, author, content, ts}]` |

---

## WebSocket protocol (`ws://host:8080/ws`)

All messages are JSON text frames. Sub-protocol name: `norichat`.

### Client → Server

```jsonc
// Authenticate immediately after connect
{"op": "AUTH", "token": "<jwt>"}

// Subscribe to channel events (loads new messages via CHANNEL_JOIN)
{"op": "CHANNEL_JOIN", "channel_id": 1}

// Unsubscribe
{"op": "CHANNEL_LEAVE", "channel_id": 1}

// Send a message
{"op": "MESSAGE_SEND", "channel_id": 1, "content": "hello"}
```

### Server → Client

```jsonc
{"op": "AUTH_OK",  "user_id": 1, "username": "vasya"}
{"op": "AUTH_FAIL","error": "invalid or expired token"}
{"op": "MESSAGE_NEW", "id": 42, "channel_id": 1,
 "author": "vasya", "content": "hello", "ts": 1700000000}
{"op": "ERROR", "error": "..."}
```

---

## Roadmap (future phases)

- **Phase 2** — Voice chat (Opus codec via miniaudio, dedicated voice channels)
- **Phase 3** — TLS (`wss://`), user avatars, server invites, message reactions
- **Phase 4** — Mobile client (Android/iOS via SDL2 + ImGui)
- **Phase 5** — Federation / multi-server

---

## License

MIT — see individual file headers.
