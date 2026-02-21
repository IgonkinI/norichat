# ─── Build stage ──────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        pkg-config \
        libssl-dev \
        libsqlite3-dev \
        libwebsockets-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY server/  ./server/
COPY shared/  ./shared/

WORKDIR /src/server
# NORICHAT_FETCH_LIBS=OFF → uses system libsqlite3-dev + libwebsockets-dev
# No heavy source compilation needed; build is fast and low-RAM.
RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DNORICHAT_FETCH_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX=/install \
    && cmake --build build --parallel \
    && cmake --install build

# ─── Runtime stage ────────────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
        libsqlite3-0 \
        libwebsockets19 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install/bin/norichat_server /usr/local/bin/norichat_server

RUN mkdir -p /data
VOLUME ["/data"]

EXPOSE 8080

RUN useradd -m -u 1001 norichat
USER norichat

ENTRYPOINT ["/usr/local/bin/norichat_server", "--db", "/data/norichat.db", "--port", "8080"]
