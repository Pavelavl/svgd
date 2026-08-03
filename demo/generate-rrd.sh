#!/bin/sh
# =============================================================================
# demo/generate-rrd.sh
#
# Generates a static, realistic-looking set of RRD time-series files for the
# svgd demo. Covers every metric referenced by config.json's metrics[] (CPU,
# RAM, swap, load, uptime, disk, interface, filesystem, postgres, processes,
# TCP, thermal). Data is a daily sinusoid + linear drift + noise, ending at
# "now" so charts are never empty regardless of when the demo is launched.
#
# No collectd, no rrdcached — just rrdtool. Safe to re-run (overwrites).
#
# Usage:
#   ./generate-rrd.sh [output_dir]
#
#   output_dir   Directory that maps to config.json -> rrd.base_path
#                (the per-host "localhost" dir). Default: ./rrd/localhost
#
# Requires: rrdtool, awk, xargs, tr. (All present in demo/Dockerfile.rrd.)
# =============================================================================
set -eu

OUT="${1:-./rrd/localhost}"

STEP=30                                  # seconds (collectd uses 5; 30 keeps demo lean)
DURATION=$((7 * 86400))                  # fill 7 days so the "Last 7 days" view works
HB=$((STEP * 4))                         # DS heartbeat
RRAS="RRA:AVERAGE:0.5:1:2000 RRA:AVERAGE:0.5:4:1500 RRA:AVERAGE:0.5:20:1500"
                                        # 30s / 120s / 600s resolutions -> covers every UI period
BATCH=4000                              # rrdtool update rows per call

NOW=$(date +%s)
NOW=$(( NOW - (NOW % STEP) ))            # snap to step boundary
START=$((NOW - DURATION))

mkdir -p "$OUT"

# -----------------------------------------------------------------------------
# create_rrd <relpath> <ds1,ds2,...>
# -----------------------------------------------------------------------------
create_rrd() {
    rel="$1"; dss="$2"
    file="$OUT/$rel"
    mkdir -p "$OUT/$(dirname "$rel")"
    rm -f "$file"
    dsdef=""
    oifs="$IFS"; IFS=','
    # shellcheck disable=SC2086
    set -- $dss
    IFS="$oifs"
    for d in "$@"; do
        dsdef="$dsdef DS:$d:GAUGE:$HB:U:U"
    done
    # shellcheck disable=SC2086
    rrdtool create "$file" --step "$STEP" --start "$((START - 1))" $dsdef $RRAS
}

# -----------------------------------------------------------------------------
# chunked_update <file> <ds1:ds2>   reads "ts:v[:v2]" lines from stdin
# -----------------------------------------------------------------------------
chunked_update() {
    file="$1"; tmpl="$2"
    tmp="${TMPDIR:-/tmp}/svgd_rrd_$$_$(basename "$file" | tr -c 'A-Za-z0-9' '_').tmp"
    cat > "$tmp"
    if [ -s "$tmp" ]; then
        # -d '\n' treats each line literally; -r skips when empty; xargs auto-batches to ARG_MAX
        xargs -r -d '\n' rrdtool update "$file" --template "$tmpl" < "$tmp"
    fi
    rm -f "$tmp"
}

# -----------------------------------------------------------------------------
# fill_rrd <relpath> <ds1,ds2> <base> <amp> <noise> <trend> <vmin> <vmax> [ds2mult]
#   Daily sinusoid (peak mid-day) + linear trend + uniform noise, clamped.
# -----------------------------------------------------------------------------
fill_rrd() {
    rel="$1"; dss="$2"; base="$3"; amp="$4"; noise="$5"
    trend="$6"; vmin="$7"; vmax="$8"; mult="${9:-1}"
    file="$OUT/$rel"
    tmpl=$(printf '%s' "$dss" | tr ',' ':')
    nds=$(printf '%s\n' "$dss" | awk -F, '{print NF}')
    awk -v step="$STEP" -v start="$START" -v now="$NOW" \
        -v base="$base" -v amp="$amp" -v noise="$noise" -v trend="$trend" \
        -v vmin="$vmin" -v vmax="$vmax" -v mult="$mult" -v nds="$nds" '
    BEGIN {
        srand(now + 1000);
        pi = 3.14159265358979323846;
        for (ts = start; ts <= now; ts += step) {
            dayfrac = (ts % 86400) / 86400.0;          # 0..1 time-of-day
            wave = amp * sin(2 * pi * dayfrac);          # daily usage curve
            drift = trend * (ts - start);                # slow trend across the window
            n = noise * (2 * rand() - 1);
            v0 = base + wave + drift + n;
            if (v0 < vmin) v0 = vmin;
            if (vmax > 0 && v0 > vmax) v0 = vmax;
            line = sprintf("%d:%.6f", ts, v0);
            if (nds >= 2) {
                v1 = v0 * mult;
                if (v1 < vmin) v1 = vmin;
                if (vmax > 0 && v1 > vmax) v1 = vmax;
                line = line sprintf(":%.6f", v1);
            }
            print line;
        }
    }' | chunked_update "$file" "$tmpl"
}

# -----------------------------------------------------------------------------
# fill_mono <relpath> <ds> <per_second> <boot_epoch>   monotonically increasing
# (used for uptime: value = (ts - boot_epoch) * per_second)
# -----------------------------------------------------------------------------
fill_mono() {
    rel="$1"; dss="$2"; per="$3"; boot="$4"
    file="$OUT/$rel"
    tmpl=$(printf '%s' "$dss" | tr ',' ':')
    awk -v step="$STEP" -v start="$START" -v now="$NOW" -v per="$per" -v boot="$boot" '
    BEGIN {
        for (ts = start; ts <= now; ts += step) {
            v = (ts - boot) * per;
            if (v < 0) v = 0;
            printf("%d:%.6f\n", ts, v);
        }
    }' | chunked_update "$file" "$tmpl"
}

# -----------------------------------------------------------------------------
# The metric table. Paths mirror the collectd layout config.json assumes.
# Concrete demo params: interface=eth0, disk=sda, process=postgres, fs=root.
# m <relpath> <dss> <base> <amp> <noise> <trend> <vmin> <vmax> [mult]
# -----------------------------------------------------------------------------
m() {
    create_rrd "$1" "$2"
    fill_rrd "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "${9:-1}"
}

# --- CPU ---
m cpu-total/percent-active.rrd              active       35     20      5      0    2    95

# --- RAM (percent) ---
m memory/percent-used.rrd                   percent      60     10      2      0   10    95
m memory/percent-cached.rrd                 percent      20      5      1      0    0     0
m memory/percent-buffered.rrd               percent      10      3      1      0    0     0

# --- Swap ---
m swap/swap-used.rrd                        value   524288000 104857600 20971520 0    0     0   # bytes (~500MB)
m swap/swap_percent-used.rrd                percent      15      5      1      0    0     0

# --- Load / Uptime ---
m load/load.rrd                             shortterm     0.8   0.5    0.2     0    0     0
BOOT=$((NOW - 30 * 86400))
create_rrd uptime/uptime.rrd                value
fill_mono uptime/uptime.rrd                 value         1 "$BOOT"           # seconds; /3600 -> ~720h

# --- Disk (sda) ---
m disk-sda/disk_ops.rrd                     ops          40     30     10      0    0     0
m disk-sda/disk_octets.rrd                  read,write 5242880 3145728 1048576 0    0     0   0.8   # bytes/s
m disk-sda/disk_time.rrd                    value        20     15      5      0    0     0

# --- Filesystem (root) ---
m df-root/df_complex-used.rrd               value  32212254720 2147483648 1073741824 0 0 0    # ~30GB
m df-root/df_complex-free.rrd               value  75161927680 2147483648 1073741824 0 0 0    # ~70GB

# --- Network (eth0) ---
m interface-eth0/if_octets.rrd              rx,tx    2500000 1500000 500000 0    0     0   0.6   # bytes/s
m interface-eth0/if_packets.rrd             rx,tx       5000 2000   500     0    0     0   0.7
m interface-eth0/if_errors.rrd              rx,tx        0.5   0.5   0.3     0    0    10   1

# --- Processes (postgres) ---
m processes-postgres/ps_cputime.rrd         user,system  0.8   0.3   0.1     0    0     0   1.2   # summed -> ~cpu time
m processes-postgres/ps_rss.rrd             rss      314572800 52428800 20971520 0 52428800 0   # bytes (~300MB)
m processes-postgres/ps_count.rrd           processes,threads  12  2  1     0    0     0   3.3

# --- PostgreSQL / TCP / Thermal ---
m postgresql-test/pg_numbackends.rrd        value        15     10      3      0    1    80
m tcpconns-80-local/tcp_connections-ESTABLISHED.rrd value 10   5   2       0    0     0
m tcpconns-80-local/tcp_connections-TIME_WAIT.rrd   value 20  10   3       0    0     0
m thermal-thermal_zone0/temperature.rrd     value        55      8      1      0   30    90

echo "[demo] Generated $(find "$OUT" -name '*.rrd' | wc -l) RRD files under $OUT"
echo "[demo] Data spans $(date -u -d "@$START" +%FT%TZ 2>/dev/null || date -u -r "$START" +%FT%TZ 2>/dev/null || echo "$START") -> $(date -u +%FT%TZ)"
