#!/bin/bash
set -e

# Use mounted config if available, otherwise use built-in
if [ -f /etc/collectd/collectd.conf ]; then
    CONFIG_FILE="/etc/collectd/collectd.conf"
else
    CONFIG_FILE="/opt/collectd/etc/collectd.conf"
fi

exec /opt/collectd/sbin/collectd -C "$CONFIG_FILE" -f "$@"
