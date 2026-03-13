#!/bin/bash
set -e

# Set hostname from environment or use default
if [ -n "$COLLECTD_HOSTNAME" ]; then
    sed -i "s/^Hostname \"localhost\"/Hostname \"$COLLECTD_HOSTNAME\"/" /opt/collectd/etc/collectd.conf
fi

# Use mounted config if available, otherwise use built-in
if [ -f /etc/collectd/collectd.conf ]; then
    CONFIG_FILE="/etc/collectd/collectd.conf"
else
    CONFIG_FILE="/opt/collectd/etc/collectd.conf"
fi

exec /opt/collectd/sbin/collectd -C "$CONFIG_FILE" -f
