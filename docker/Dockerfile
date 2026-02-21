# ─── Build stage ──────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        libssl-dev \
        libz-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY server/  ./server/
COPY shared/  ./shared/

WORKDIR /src/server
RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/install \
    && cmake --build build --parallel "$(nproc)" \
    && cmake --install build

# ─── Runtime stage ────────────────────────────────────────────────────────────
FROM debian:bookworm-slim

# Only the libraries actually needed at runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install/bin/norichat_server /usr/local/bin/norichat_server

# Data directory for the SQLite database
RUN mkdir -p /data
VOLUME ["/data"]

EXPOSE 8080

# Run as non-root
RUN useradd -m -u 1001 norichat
USER norichat

ENTRYPOINT ["/usr/local/bin/norichat_server", "--db", "/data/norichat.db", "--port", "8080"]
