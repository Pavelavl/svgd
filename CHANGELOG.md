# Changelog

All notable changes to **svgd** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Tracked on the `master` branch. New entries are added here as features land;
they move to a versioned section on release.

### Added
- **Pluggable metric sources (Phase 2)** — `svgd` reads from multiple backends
  through a `metric_source_t` seam introduced at `rrd_fetch_data()`. A new
  per-metric `source` field in `config.json` selects the backend (default
  `"rrd"`, so existing configs are unchanged):
  - `rrd` — the existing RRD-file path, behavior byte-identical to before.
  - `proc` — live `/proc` reads (`cpu` from `/proc/stat`, `load` from
    `/proc/loadavg`) assembled into an in-memory `MetricData` with **no RRD and
    no disk** — for ultra-constrained "no storage" setups.
  - `prometheus` — parse Prometheus text-exposition over HTTP
    (`prometheus_url`) into `MetricData`, so `svgd` can visualize metrics
    exported by other systems.
  New translation units `src/metric_source.{c,h}`, `src/proc_source.{c,h}`,
  `src/prometheus_source.{c,h}`; the existing `src/rrd/cache.c` is reused
  (source-agnostic cache key). Turns `svgd` from an "RRD viewer" into a
  universal lightweight metrics visualizer. (`proc`/`prometheus` currently
  yield a single-point live series; the `prometheus` source is HTTP-only, no
  TLS — see `docs/configuration.md`.)
- **SVG theme system** — `light` / `dark` / `high-contrast` render themes in
  `src/scripts/generate_svg.js` (`THEMES` map + `resolveTheme()`), chosen
  per-request via the `?theme=` query parameter or globally via `server.theme`
  in `config.json` (priority: query > config > `light`). The `light` palette is
  byte-identical to the previous default. Adding a theme is a JS-only edit, no
  recompile.
- **Metric & theme gallery** — new `docs/gallery.md` page: theme showcase,
  metric-type recipes (CPU, RAM, network, disk, process RSS, PostgreSQL) and
  ready-made `config.json` snippets for common scenarios (a Docker host, a
  Raspberry Pi), plus a "custom theme" howto. Sample SVGs under
  `docs/assets/gallery/`; cross-linked from `configuration.md` and
  `architecture.md`.
- **C unit-test harness** — `tests/c/` (`minitest.h` + `run.sh` + 7 test
  binaries) and a `make test-c` target, wired as the first prerequisite of
  `make test` so pure-logic regressions fail fast before the integration suite.
  Covers logic previously exercised only end-to-end: RRA step selection
  (`select_step_from_rras`, extracted from `select_optimal_step`), metric-config
  lookup (`find_metric_config`), path templating (`build_rrd_path` /
  `extract_param_from_path`, extracted to `src/path_util.{c,h}`), and
  `config.json` parsing. ~60 test cases.
- **svgd-collect standalone CI** — `.github/workflows/ci.yml` in the
  `svgd-collect` submodule builds and runs its 17 unit tests + 3 integration
  suites on push/PR (previously the submodule had no CI of its own).

### Changed
- _(nothing yet)_

### Fixed
- **`load_config` crash on a partial `config.json`** — in `src/cfg.c`, when the
  `server`, `rrd`, or `js` section was absent, an unbalanced Duktape stack (the
  pop happened only inside the `if (truthy)` branch) caused a `duk_fatal` /
  `SIGABRT` on the next section read. The pop is now unconditional. Latent in
  practice (production configs always carried all sections); regression tests
  added in `tests/c/test_config.c`.

### Security
- _(nothing yet)_

## [0.1.0] — 2026-08-04

First public, benchmarked release of the lightweight C monitoring system.
`svgd` renders SVG charts from RRD time-series files through a data-driven
(config + embedded JS) pipeline that holds ~0% CPU / ~10 MB RAM under load. The
release ships a full self-contained stack — renderer + HTTP gateway + web UI +
optional `svgd-collect` collector — plus Grafana and Prometheus integration,
packaging, and documentation.

### Added
- **Open-source packaging & community** — MIT `LICENSE`; English-primary
  `readme.md` (badges, "Why svgd?" highlights, benchmark tables) + Russian
  mirror `readme.ru.md`; `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`,
  `SECURITY.md`; GitHub issue/PR templates under `.github/`.
- **Backend (`src/`, `svgd` binary)** — reads RRD time-series and renders SVG.
  Selectable transport via `config.json` → `server.protocol`:
  - **LSRP mode** (default): binary wire protocol over TCP, thread pool, caching
    (RRD-data TTL hash table + per-thread Duktape JS contexts pre-warmed at
    startup). The high-throughput path.
  - **HTTP mode**: single-threaded plain-HTTP fallback (caching disabled).
  - Shared business logic in `src/handler.c` (`handler_process()`).
- **SVG rendering in JavaScript** — the Duktape engine runs
  `src/scripts/generate_svg.js` against serialized `MetricData`. Chart
  appearance and behavior are controlled from JS without recompiling C — the
  primary extensibility surface.
- **HTTP gateway + web UI (`gate/`, `svgd-gate` binary)** — serves the browser
  UI from `gate/static/` (dynamic panels, time-range/auto-refresh controls,
  search/filter, fullscreen, SVG export, light/dark theme, dashboard
  export/import, HTML snapshots); proxies metric requests to the backend
  selected by `?datasource=` or a configured default; **multi-datasource
  routing** (one gate fronts N backends) with runtime CRUD via `/_datasources`.
- **Grafana datasource compatibility** — `svgd-gate` serves the
  simpod / classic-SimpleJson structured datasource contract at `/grafana/*`
  (`search`, `query`, `annotations`, connection check). Verified end-to-end
  with a real Grafana container. The gate is a thin forwarder; JSON assembly
  happens in the backend's existing Duktape engine — no new dependencies. Gate
  request buffer raised to 64 KB.
- **Data-driven metrics** — `config.json` → `metrics[]` maps an `endpoint` to an
  `rrd_path`; paths may contain `%s` for a URL path parameter
  (`requires_param: true`, e.g. `GET /ram/process/postgres`). Supports value
  transforms (`divide`, `sum`, `multiply`) and percentage formatting. Parsed in
  `src/cfg.c`, applied in `src/handler.c`.
- **Optional JWT-like auth (HMAC-SHA256 via OpenSSL)** — configured by
  `gate/auth/auth.json`. Protected API endpoints require a `Bearer` token;
  static files and `/_auth/*` are always public. Off by default (endpoints
  return 401 when `auth.json` is absent).
- **`svgd-collect` — drop-in collector (submodule `svgd-collect/`)** — a
  standalone C collector
  ([`Pavelavl/svgd-collect`](https://github.com/Pavelavl/svgd-collect)) that
  replaces collectd as the data source. Reads `/proc`/`/sys` and writes RRDs in
  the identical collectd layout, so `config.json` works unchanged (drop-in).
  **11 readers** (cpu, load, uptime, memory, swap, interface, disk, df,
  processes, thermal, tcpconns); DS names/types/RRA verified against real
  collectd output. Optional **rrdcached routing** (`collect.json` →
  `rrdcached_addr`; RRD creation stays direct, hot-path updates are routed,
  with dead-daemon fallback to direct write), **MIN/MAX RRAs** alongside
  AVERAGE, reader **error logging**, and a testable reader **registry**. Removes
  the dependency on the stalled collectd project (last stable 5.12.0, 2021).
- **Prometheus `/metrics` exposition** — `svgd-collect` exposes an opt-in
  `/metrics` endpoint in
  [text exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/)
  (set `metrics_addr` in `collect.json`). Plain-C-sockets HTTP listener on a
  dedicated thread; gauge/counter types are derived from the collectd DS
  definitions; a threadsafe two-buffer snapshot of the last collection cycle
  backs each scrape so collection is never blocked. Makes the stack visible to
  Prometheus / Grafana / any modern observability consumer.
- **One-command quickstart demo** — `docker compose -f docker-compose.demo.yml
  up` (or `make demo`) yields a working dashboard on `:8080` with **no collectd,
  no rrdcached, no manual config**. An `rrd-init` one-shot regenerates fresh
  demo RRDs on every start (via `demo/generate-rrd.sh`) so charts never go
  stale. Login password `demo`.
- **Documentation site** — mkdocs-material site (`docs/`, `mkdocs.yml`) with
  landing, architecture, quickstart, installation, configuration pages, and an
  honest **comparison** vs Monitorix/Munin/Netdata; deployed to GitHub Pages via
  `.github/workflows/docs.yml`. Public `ROADMAP.md`.
- **Installation & deployment** — `make install` (honors `PREFIX`/`DESTDIR`,
  non-clobbering config install) + systemd units for `svgd` and `svgd-gate`
  (`.infra/systemd/`) + post-install notes (`.infra/install-notes.md`).
- **Release packaging** — `make dist` produces a distributable source tarball
  (`svgd-<version>.tar.gz`; version from `git describe --tags --always`, `lsrp`
  flattened in so packagers need no `git submodule init`). A new
  `.github/workflows/release.yml` (triggered by `v*` semver-tag pushes)
  cross-compiles Linux **amd64 + arm64** binary packages using the same Docker
  toolchain as `deploy.yml`, builds the source tarball, and publishes a GitHub
  Release whose notes are extracted from this CHANGELOG. An AUR `PKGBUILD`
  (`packaging/aur/svgd/PKGBUILD`) builds from the release tarball and installs
  to Arch-standard paths with a `sysusers.d` fragment for the `svgd` service
  user.
- **Benchmarking harness** — `make test-bench-svgd` (svgd alone) and
  `make test-bench` (svgd vs RRDtool CGI vs Graphite, via Docker), plus
  `make generate-charts` / `make generate-report`. Reported throughput up to
  ~2830 RPS at ~0% CPU / ~10 MB RAM.
- **Cross-compilation and deployment** — `make ARCH=arm64 build`; `deploy.yml`
  produces a split-architecture release (ARM64 gate, x86_64 backends).
- **Test suites** — Go integration tests (`make test`, `make test-e2e`,
  `make test-load`) in a Go workspace under `tests/`, and Python/Selenium UI
  tests (`make test-ui`). Tests require `REPO_ROOT`.
- **Docker** — `Dockerfile` / `docker-compose.yml` for single-backend runs and
  `docker-compose.multi.yml` for the multi-datasource demo.

### Known limitations
- HTTP backend mode is single-threaded with caching disabled; use LSRP mode for
  production throughput.
- `svgd` does not terminate TLS — deploy behind a reverse proxy in production.
- The Prometheus `/metrics` endpoint has no TLS/auth (matches `node_exporter`
  defaults); bind it to a private interface or protect it with a firewall.
- `svgd-collect`'s `thermal` and `tcpconns` readers have no reference collectd
  RRDs in this setup; verified structurally and live, not byte-for-byte.
- The `lsrp/` directory is a git submodule; protocol changes belong upstream at
  [Pavelavl/lsrp](https://github.com/Pavelavl/lsrp), not in this repo.

[Unreleased]: https://github.com/Pavelavl/svgd/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Pavelavl/svgd/releases/tag/v0.1.0
