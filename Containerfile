# ============================================================================
# Multi-stage build for the router microservice
# Stage 1: Builder (AMD ROCm base image)
# ============================================================================
FROM rocm/dev-ubuntu-24.04 AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++-14 \
    ninja-build \
    libssl-dev \
    libpq-dev \
    protobuf-compiler \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel

# ============================================================================
# Stage 2: Runtime (minimal image)
# ============================================================================
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y \
    libssl3 \
    libpq5 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -u 10001 -s /usr/sbin/nologin router

COPY --from=builder /src/build/router /usr/local/bin/router

USER router
EXPOSE 9090
ENTRYPOINT ["/usr/local/bin/router"]
