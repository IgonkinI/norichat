# NoriChat

Lightweight open-source messenger — Phase 2 (text chat + voice channels).

```
Server: Linux  / C++17 / libwebsockets · SQLite3 · OpenSSL
Client: Windows / C++17 / ImGui+SDL2+OpenGL3 · libwebsockets · libcurl · miniaudio
```

---

## Features

| Feature | Status |
|---------|--------|
| Register / Login (JWT) | ✅ |
| Text channels | ✅ |
| Real-time messages (WebSocket) | ✅ |
| Message edit & delete | ✅ |
| Online / offline presence | ✅ |
| Create channels (text or voice) | ✅ |
| Voice channels (PCM over WebSocket) | ✅ |
| TLS (`wss://`) | planned |
| Server invites / reactions | planned |

---

## Directory structure

```
norichat/
├── server/              # Linux server binary
│   ├── src/
│   │   ├── main.cpp
│   │   ├── api/         # HTTP REST handlers (libwebsockets HTTP)
│   │   ├── ws/          # WebSocket session manager + voice relay
│   │   ├── db/          # SQLite3 layer
│   │   └── auth/        # SHA-256 password hash + HS256 JWT
│   └── CMakeLists.txt
├── client/              # Windows GUI client
│   ├── src/
│   │   ├── main.cpp     # SDL2 window · OpenGL3 · Dear ImGui loop
│   │   ├── net/         # HttpClient (libcurl) · WsClient · VoiceClient (miniaudio)
│   │   └── ui/          # LoginScreen · MainScreen
│   └── CMakeLists.txt
├── shared/
│   └── protocol/messages.h   # opcode/path constants shared by both sides
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
- CMake ≥ 3.16, Git
- [vcpkg](https://github.com/microsoft/vcpkg) with the `x64-windows-static` triplet:

```powershell
vcpkg install sdl2:x64-windows-static curl:x64-windows-static opengl:x64-windows-static
```

All other dependencies (Dear ImGui, libwebsockets, nlohmann/json, miniaudio) are fetched automatically by CMake.

### Build with Visual Studio

```powershell
cd norichat\client
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

### Build with Ninja / LLVM

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build
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
| POST | `/api/channels` | Bearer | `{server_id, name, type}` | `{id, server_id, name, type}` |
| GET | `/api/members?server_id=X` | Bearer | – | `[{id, username}]` |
| GET | `/api/messages?channel_id=X&limit=50` | Bearer | – | `[{id, channel_id, author, content, ts}]` |

---

## WebSocket protocol (`ws://host:8080/ws`)

All messages are JSON text frames. Sub-protocol name: `norichat`.

### Client → Server

```jsonc
// Authenticate immediately after connect
{"op": "AUTH", "token": "<jwt>"}

// Subscribe to a text channel (receive new messages)
{"op": "CHANNEL_JOIN",  "channel_id": 1}
{"op": "CHANNEL_LEAVE", "channel_id": 1}

// Send / edit / delete a text message
{"op": "MESSAGE_SEND",   "channel_id": 1, "content": "hello"}
{"op": "MESSAGE_EDIT",   "message_id": 42, "content": "updated"}
{"op": "MESSAGE_DELETE", "message_id": 42}

// Join / leave a voice channel
{"op": "VOICE_JOIN",  "channel_id": 5}
{"op": "VOICE_LEAVE", "channel_id": 5}

// Stream a 20 ms audio frame (base64 PCM, 16 kHz mono int16)
{"op": "VOICE_DATA", "channel_id": 5, "data": "<base64>"}
```

### Server → Client

```jsonc
{"op": "AUTH_OK",  "user_id": 1, "username": "vasya",
 "online": [{"user_id": 2, "username": "petya"}]}
{"op": "AUTH_FAIL","error": "invalid or expired token"}

{"op": "MESSAGE_NEW",     "id": 42, "channel_id": 1,
 "author": "vasya", "author_id": 1, "content": "hello", "ts": 1700000000}
{"op": "MESSAGE_EDITED",  "message_id": 42, "channel_id": 1, "content": "updated"}
{"op": "MESSAGE_DELETED", "message_id": 42, "channel_id": 1}

{"op": "USER_ONLINE",  "user_id": 2, "username": "petya"}
{"op": "USER_OFFLINE", "user_id": 2}

// Confirmed voice join, includes current participants
{"op": "VOICE_JOIN_OK", "channel_id": 5,
 "participants": [{"user_id": 2, "username": "petya"}]}
{"op": "VOICE_JOINED", "channel_id": 5, "user_id": 2, "username": "petya"}
{"op": "VOICE_LEFT",   "channel_id": 5, "user_id": 2}
// Relayed audio frame from another participant
{"op": "VOICE_DATA", "channel_id": 5, "user_id": 2, "data": "<base64>"}

{"op": "ERROR", "error": "..."}
```

### Voice channel UI

- Text channels appear as `# name` in the sidebar.
- Voice channels appear as `> name` (green tint).
- Click a voice channel to **join**; click again to **leave**.
- While active, a `[mic]` indicator is shown next to your username and the Members panel shows an **IN VOICE** section.
- When creating a channel, choose **Text** or **Voice** via the radio button in the dialog.

---

## Roadmap

- **Phase 3** — Opus codec (currently raw PCM), TLS (`wss://`)
- **Phase 4** — User avatars, server invites, message reactions
- **Phase 5** — Mobile client (Android/iOS via SDL2 + ImGui)
- **Phase 6** — Federation / multi-server

---

## License

MIT — see individual file headers.
