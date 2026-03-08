# Collectd with RRDtool and PostgreSQL support
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    wget \
    bzip2 \
    gnupg \
    ca-certificates \
    librrd-dev \
    libpq-dev \
    libgcrypt20-dev \
    libperl-dev \
    libcurl4-openssl-dev \
    libxml2-dev \
    libmnl-dev \
    libyajl-dev \
    && rm -rf /var/lib/apt/lists/*

# Download and build collectd
WORKDIR /tmp
RUN wget -q https://github.com/collectd/collectd/releases/download/collectd-5.12.0/collectd-5.12.0.tar.bz2 \
    && tar xjf collectd-5.12.0.tar.bz2 \
    && cd collectd-5.12.0 \
    && ./configure \
        --prefix=/opt/collectd \
        --disable-python \
        --disable-werror \
        --enable-postgresql \
        --enable-rrdtool \
        --enable-aggregation \
        --enable-cpu \
        --enable-df \
        --enable-disk \
        --enable-interface \
        --enable-load \
        --enable-memory \
        --enable-processes \
        --enable-swap \
        --enable-uptime \
        LIBS="-lgcrypt" \
    && make -j$(nproc) \
    && make install

# Runtime image
FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    librrd8 \
    libpq5 \
    libgcrypt20 \
    libcurl4 \
    libxml2 \
    libyajl2 \
    libmnl0 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Copy built collectd
COPY --from=builder /opt/collectd /opt/collectd

# Copy configuration files
COPY collectd.conf /opt/collectd/etc/collectd.conf
COPY collectd.conf.d /opt/collectd/etc/collectd.conf.d

# Create directories for RRD data
RUN mkdir -p /opt/collectd/var/lib/collectd/rrd /var/run/collectd

# Copy entrypoint
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Create non-root user
RUN useradd -r -s /bin/false collectd \
    && chown -R collectd:collectd /opt/collectd/var

USER collectd

VOLUME ["/opt/collectd/var/lib/collectd/rrd"]

EXPOSE 25826/udp

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD test -d /opt/collectd/var/lib/collectd/rrd/localhost || exit 1

ENTRYPOINT ["/entrypoint.sh"]
CMD ["-f"]
