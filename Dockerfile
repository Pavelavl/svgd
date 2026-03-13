# Build svgd on top of pre-built base
ARG SVGD_BASE=svgd-base:latest
FROM ${SVGD_BASE} AS builder

COPY . /build
WORKDIR /build

RUN make build

# Stage 2: Runtime
FROM debian:bookworm-slim

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

CMD ["./bin/svgd", "./config.json"]
