# Lightweight config-only image - builds in seconds
ARG COLLECTD_BASE=svgd-collectd-base:latest
FROM ${COLLECTD_BASE}

# Copy configuration files
COPY collectd.conf /opt/collectd/etc/collectd.conf
COPY collectd.conf.d /opt/collectd/etc/collectd.conf.d

# Set permissions for config editing
RUN chown -R collectd:collectd /opt/collectd/etc

# Copy entrypoint
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

USER collectd

EXPOSE 25826/udp

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD test -d /opt/collectd/var/lib/collectd/rrd/$(cat /opt/collectd/etc/collectd.conf | grep -m1 ^Hostname | cut -d'"' -f2) || exit 1

ENTRYPOINT ["/entrypoint.sh"]
CMD []
