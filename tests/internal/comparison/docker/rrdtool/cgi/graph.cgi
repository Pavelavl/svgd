#!/bin/bash
# CGI script for generating RRD graphs

echo "Content-Type: image/svg+xml"
echo ""

RRD_PATH="/var/lib/collectd/rrd/localhost"
RRD_FILE="$RRD_PATH/cpu-total/percent-active.rrd"

if [ ! -f "$RRD_FILE" ]; then
    echo "<svg><text x='10' y='20'>RRD file not found: $RRD_FILE</text></svg>"
    exit 0
fi

rrdtool graph - -a SVG \
  --width 800 --height 300 \
  --start -3600 \
  --title "CPU Usage" \
  --vertical-label "%" \
  --lower-limit 0 \
  --upper-limit 100 \
  DEF:cpu=$RRD_FILE:value:AVERAGE \
  AREA:cpu#2ecc71:"CPU" \
  LINE1:cpu#27ae60
