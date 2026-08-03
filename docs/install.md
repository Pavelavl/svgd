# Installation

Full build, run, package, and deployment reference. For the fast path see
[Quick start](quickstart.md).

## Requirements

- **librrd-dev** — RRD read/write library.
- **duktape-dev** — embedded JavaScript engine (SVG rendering).
- **libssl-dev** — HMAC-SHA256 for optional `svgd-gate` auth.
- **gcc** + **make** — build toolchain.
- **jq** — used by the Makefile to read `config.json` (optional).
- A data source writing RRD files: **collectd** or **svgd-collect** (bundled).

### Install dependencies (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install librrd-dev duktape-dev libssl-dev gcc make jq
```

## Get the source

```bash
git clone https://github.com/Pavelavl/svgd.git
cd svgd
git submodule update --init --recursive   # REQUIRED: fetches lsrp/ and svgd-collect/
```

The submodule init is **required** — `lsrp` is linked into both binaries.

## Build

```bash
make build            # bin/svgd (backend) + bin/svgd-gate (gateway)
make build-backend    # backend only
make clean
```

`make build*` also creates a `scripts` symlink at the repo root pointing to
`src/scripts/`, because the default `config.json` references
`./scripts/generate_svg.js`. `make clean` removes it.

### Cross-compile for ARM64

```bash
make ARCH=arm64 build    # uses aarch64-linux-gnu-gcc
```

The deploy workflow builds a **split architecture**: `svgd-gate` is
cross-compiled to ARM64, backends to x86_64 — one gate fronts many backends.

## Run

Both binaries take their configuration from `config.json` (copy from
`config.sample.json`).

```bash
./bin/svgd ./config.json                                # backend
./bin/svgd-gate 127.0.0.1 8081 8080 ./gate/static       # gateway
```

Or via the Makefile:

```bash
make run-backend    # svgd backend on config.json's tcp_port (default 8081)
make run            # svgd-gate on :8080 (expects a backend on config's port)
```

Then open <http://localhost:8080>.

The backend speaks either **LSRP** (binary, default — thread pool + caching) or
**plain HTTP** (single-threaded fallback, caching disabled), selected by
`config.json` → `server.protocol`. See [Architecture](architecture.md).

## Install to the system

`svgd` ships a non-clobbering `make install` target that honors `PREFIX` and
`DESTDIR`:

```bash
make install PREFIX=/usr/local DESTDIR=/tmp/pkg
```

It installs:

- `bin/svgd`, `bin/svgd-gate` → `$PREFIX/bin/`
- `gate/static/**` → `$PREFIX/share/svgd/gate/static/`
- `src/scripts/generate_svg.js` → `$PREFIX/share/svgd/scripts/`
- `config.sample.json` → `$PREFIX/share/svgd/` (only if not already present)

### systemd units

Ready-made unit files live in `.infra/systemd/`:

- `svgd.service` — the backend.
- `svgd-gate.service` — the gateway.

Copy them to `/etc/systemd/system/`, adjust paths/ports, then:

```bash
sudo systemctl enable --now svgd svgd-gate
```

## rrdcached (optional)

`rrdcached` batches and buffers RRD writes, which dramatically lowers disk I/O
when many metrics are being written.

```bash
sudo rrdcached -p /var/run/rrdcached.pid \
               -l unix:/var/run/rrdcached.sock \
               -B -F \
               -b /opt/collectd/var/lib/collectd/rrd \
               -j /var/lib/rrdcached/journal \
               -f 3600 -w 1800 -z 900
```

In `config.json`:

```json
"server": {
  "rrdcached_addr": "unix:/var/run/rrdcached.sock"
}
```

## Docker

```bash
make docker-build     # build the image
make docker-up        # run (single backend)
make docker-logs
make docker-down

make docker-test      # run the test suite in Docker
```

For the multi-datasource demo (one gate fronting two backends), see
`docker-compose.multi.yml`.

## Data sources

### Option A — svgd-collect (drop-in collectd replacement)

A small standalone C collector bundled as `svgd-collect/`. Reads `/proc`/`/sys`
and writes the **same RRD files in the same layout** as collectd, so
`config.json` works unchanged.

```bash
git submodule update --init --recursive   # fetch svgd-collect/
cd svgd-collect && make build              # → bin/svgd-collect
./bin/svgd-collect collect.json
```

Point `collect.json`'s output directory at `config.json`'s `rrd.base_path`.
Covers cpu, load, uptime, memory, swap, interface, disk, df, processes.

### Option B — collectd

Install collectd and configure it to write RRDs. Example configurations live in
`.infra/collectd/` (cpu, df, disk, load, network, processes, swap, tcpconns,
thermal, uptime). Set collectd's `DataDir` to match `rrd.base_path`.

collectd's last stable release was 5.12.0 (September 2021); `svgd-collect`
exists precisely to remove that dependency. See the
[Comparison](comparison.md#svgd-collect-vs-collectd) page for context.

## Verification

After starting both processes:

```bash
# Metrics list
curl http://localhost:8080/_config/metrics

# A chart
curl http://localhost:8080/cpu
```

If charts do not render, check that RRD files exist under `rrd.base_path` and
that the configured `endpoint` → `rrd_path` mapping is correct
(see [Configuration](configuration.md)).

## Troubleshooting

**The server won't start.**

```bash
ss -tulpn | grep -E '8080|8081'      # ports in use?
ls -la /opt/collectd/var/lib/collectd/rrd/   # RRD permissions
./bin/svgd ./config.json             # run in foreground, read logs
```

**Charts don't render.**

```bash
ls /opt/collectd/var/lib/collectd/rrd/localhost/    # RRDs present?
curl http://localhost:8080/_config/metrics           # gate → backend OK?
curl http://localhost:8080/cpu
```

**Low performance.**

1. Enable `rrdcached` in `config.json`.
2. Increase `server.thread_pool_size`.
3. Make sure `server.protocol` is `lsrp` (not `http` — HTTP mode disables caching).
4. Lower the dashboard auto-refresh interval.
