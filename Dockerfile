# Stage 1: Build dependencies and svgd
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc libc6-dev make librrd-dev jq git curl ca-certificates xz-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp

# Build duktape from source
RUN curl -fsSL https://github.com/svaarala/duktape/releases/download/v2.7.0/duktape-2.7.0.tar.xz -o duktape.tar.xz \
    && tar xf duktape.tar.xz \
    && cd duktape-2.7.0 \
    && gcc -O2 -shared -fPIC -o libduktape.so src/duktape.c -lm \
    && install -m 644 libduktape.so /usr/local/lib/ \
    && install -m 644 src/duktape.h src/duk_config.h /usr/local/include/ \
    && ldconfig \
    && rm -rf /tmp/duktape*

COPY . /build
WORKDIR /build

RUN make build

# Stage 2: Runtime
FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    librrd8 ca-certificates curl netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Copy duktape from builder
COPY --from=builder /usr/local/lib/libduktape.* /usr/lib/
COPY --from=builder /usr/local/include/duktape.h /usr/include/

# Copy binaries
COPY --from=builder /build/bin/svgd /app/bin/svgd
COPY --from=builder /build/bin/svgd-gate /app/bin/svgd-gate

# Copy static files for gate
COPY --from=builder /build/gate/static /app/gate/static

# Copy JS scripts for SVG generation
COPY --from=builder /build/src/scripts /app/src/scripts

# Copy default config
COPY --from=builder /build/config.json /app/config.json

WORKDIR /app

EXPOSE 8080 8081

# Default: run backend (can be overridden in docker-compose)
CMD ["./bin/svgd", "./config.json"]
