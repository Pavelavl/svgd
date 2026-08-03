# Quick start

This page gets you from a clean clone to a running `svgd` dashboard in the
smallest number of steps. For the full build/reference, see
[Installation](install.md) and [Configuration](configuration.md).

## 1. Clone with submodules

`svgd` ships two git submodules: [`lsrp`](https://github.com/Pavelavl/lsrp)
(the binary wire protocol) and
[`svgd-collect`](https://github.com/Pavelavl/svgd-collect) (a drop-in collectd
replacement). Both are required at build time.

```bash
git clone https://github.com/Pavelavl/svgd.git
cd svgd
git submodule update --init --recursive
```

## 2. Install build dependencies

Debian/Ubuntu names:

```bash
sudo apt update
sudo apt install librrd-dev duktape-dev gcc make jq libssl-dev
```

- `librrd-dev` — RRD read/write (librrd).
- `duktape-dev` — the embedded JavaScript engine that renders SVG.
- `libssl-dev` — for optional JWT-like auth in `svgd-gate`.
- `jq` — used by the Makefile to read `config.json` (optional but convenient).

> **Note:** the README of some downstream packages calls the JS engine package
> `libduktape-dev`; on Debian/Ubuntu the correct name is `duktape-dev`.

## 3. Build

```bash
make build          # builds bin/svgd (backend) and bin/svgd-gate (gateway)
# or backend only:
make build-backend
```

`make build*` also creates a `scripts` symlink at the repo root pointing to
`src/scripts/`, because the default `config.json` references
`./scripts/generate_svg.js`.

## 4. Provide RRD data

`svgd` reads RRD files written by **collectd** or by the bundled
**svgd-collect**. You need RRD files present at the path configured in
`config.json` → `rrd.base_path` (default
`/opt/collectd/var/lib/collectd/rrd/localhost`).

=== "Use svgd-collect (no collectd dependency)"

    The standalone C collector writes the same RRD files in the same layout as
    collectd, so `config.json` works unchanged.

    ```bash
    cd svgd-collect && make build          # → bin/svgd-collect
    cd ..
    # edit collect.json so its output dir == config.json's rrd.base_path
    ./svgd-collect/bin/svgd-collect svgd-collect/collect.json
    ```

    It reads `/proc`/`/sys` and writes cpu, load, uptime, memory, swap,
    interface, disk, df, and processes RRDs.

=== "Use collectd"

    Install and configure collectd. Example configs live in
    [.infra/collectd/](https://github.com/Pavelavl/svgd/tree/master/.infra/collectd).
    A typical collectd setup writes RRDs to
    `/opt/collectd/var/lib/collectd/rrd/<host>/`.

    For better read performance under load, run `rrdcached` and point
    `config.json` → `server.rrdcached_addr` at its socket. See
    [Installation](install.md#rrdcached-optional).

## 5. Run

Two processes are involved: the **backend** (`svgd`, renders SVG from RRD) and
the **gateway** (`svgd-gate`, web UI + HTTP proxy).

```bash
make run-backend   # svgd backend on the port from config.json (default 8081)
make run           # svgd-gate web UI on http://localhost:8080
```

Open <http://localhost:8080>. You should see the dashboard with your metrics.

## 6. Fetch a chart directly

```bash
# SVG chart for CPU over the last hour
curl http://localhost:8080/cpu

# CPU over 24 hours
curl http://localhost:8080/cpu?period=86400

# Per-process memory
curl http://localhost:8080/ram/process/postgres
```

The response is a self-contained SVG — you can save it, `<img>`-embed it, or
inline it in an HTML page.

## 7. Copy the configs

```bash
cp config.sample.json config.json      # backend config
# datasources.json is auto-created by svgd-gate on first run from CLI args
```

For auth (optional), copy `gate/auth/auth.example.json` to
`gate/auth/auth.json`. See [Configuration](configuration.md).

---

## Next

- [Installation](install.md) — full build, cross-compile, Docker, systemd.
- [Architecture](architecture.md) — request flow, the two caches, JS rendering.
- [Configuration](configuration.md) — the `metrics[]` array, `%s` path params,
  transforms, Grafana datasource.
