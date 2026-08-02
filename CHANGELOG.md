# Changelog

All notable changes to **svgd** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Note on versioning:** svgd does not yet follow a strict release cadence.
> Version numbers below are preliminary and used to track milestones; the
> authoritative source of changes between releases is the `git log` on
> `master`. Once a `1.0.0` is cut, this changelog becomes the canonical
> per-release summary.

## [Unreleased]

Tracked on the `master` branch. New entries are added here as features land;
they move to a versioned section on release.

### Added
- **Grafana datasource compatibility** — svgd-gate now serves the simpod /
  classic-SimpleJson structured datasource contract at `/grafana/*`
  (`GET /grafana`, `POST /grafana/search`, `POST /grafana/query`,
  `POST /grafana/annotations`). A Grafana instance can plot svgd metrics as
  time-series with zero per-panel configuration. The gate forwards requests to
  the backend, which parses the query body with its existing Duktape engine and
  assembles Grafana time-series JSON — no new dependencies. Reuses the gate's
  existing Bearer-token auth (set `Authorization: Bearer <token>` as a custom
  header in the datasource config, Access = Server). Gate request buffer raised
  to 64 KB to accept Grafana POST bodies.

### Changed
- _(nothing yet)_

### Fixed
- _(nothing yet)_

### Security
- _(nothing yet)_

## [0.1.0] — initial public release

First public, benchmarked release of the lightweight C monitoring system.
Rendering is data-driven (config + embedded JS) and resource use stays near
~0% CPU / ~10 MB RAM under load. See `readme.md` for the full benchmark
comparison against Graphite and RRDtool CGI.

### Added
- **Open-source packaging & community** — MIT `LICENSE`; English-primary
  `readme.md` with badges, a "Why svgd?" highlights section, and benchmark
  tables preserved verbatim, plus a Russian mirror at `readme.ru.md`;
  `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`; GitHub issue/PR
  templates under `.github/`.
- **Installation & deployment** — `make install` target (honors `PREFIX`/
  `DESTDIR`, installs binaries/static UI/JS, guarded non-clobbering config
  install), systemd units for `svgd` and `svgd-gate` (`.infra/systemd/`),
  and post-install notes (`.infra/install-notes.md`).
- **Backend (`src/`, `svgd` binary)** — reads RRD time-series files and renders
  SVG charts. Selectable transport via `config.json` → `server.protocol`:
  - **LSRP mode** (default): binary wire protocol over TCP, thread pool, and
    caching (RRD data TTL hash table + per-thread Duktape JS contexts,
    pre-warmed at startup). This is the high-throughput path.
  - **HTTP mode**: single-threaded plain-HTTP fallback. Caching is disabled in
    this mode (guarded by `protocol != "http"`).
  - Shared business logic in `src/handler.c` (`handler_process()`).
- **SVG rendering in JavaScript** — the Duktape engine runs
  `src/scripts/generate_svg.js` against serialized `MetricData`. Chart
  appearance and behavior are controlled from JS, the primary extensibility
  surface, without recompiling C.
- **HTTP gateway + web UI (`gate/`, `svgd-gate` binary)**:
  - Serves the browser UI from `gate/static/` (dynamic panels, time-range and
    auto-refresh controls, search/filter, fullscreen, SVG export, light/dark
    theme, dashboard export/import, HTML snapshots).
  - Proxies metric requests to the backend, selected by a `?datasource=`
    query param or a configured default.
  - **Multi-datasource routing**: one gate can front multiple backends.
    Datasources are loaded from `datasources.json` and managed at runtime via
    the `/_datasources` CRUD API.
- **Data-driven metrics** — `config.json` → `metrics[]` maps an `endpoint` to
  an `rrd_path`. Paths may contain `%s` for a URL path parameter
  (`requires_param: true`, e.g. `GET /ram/process/postgres`). Parsed in
  `src/cfg.c`, applied in `src/handler.c`. Supports value transforms
  (`divide`, `sum`, `multiply`) and percentage formatting.
- **Optional JWT-like auth (HMAC-SHA256 via OpenSSL)** — configured by
  `gate/auth/auth.json`. Protected API endpoints require a `Bearer` token;
  static files and `/_auth/*` are always public. Off by default (endpoints
  return 401 when `auth.json` is absent).
- **Benchmarking harness** — `make test-bench-svgd` (svgd alone) and
  `make test-bench` (svgd vs RRDtool CGI vs Graphite, via Docker), plus
  `make generate-charts` / `make generate-report`. Reported throughput up to
  ~2830 RPS at ~0% CPU / ~10 MB RAM, with ~28x lower latency under load vs
  early revisions and a ~3–5x throughput gain over the benchmark window.
- **Cross-compilation and deployment** — `make ARCH=arm64 build` and the
  deploy workflow produce a split-architecture release (ARM64 gate, x86_64
  backends).
- **Test suites** — Go integration tests (`make test`, `make test-e2e`,
  `make test-load`) in a Go workspace under `tests/`, and Python/Selenium UI
  tests (`make test-ui`). Tests require `REPO_ROOT`.
- **Docker** — `Dockerfile` / `docker-compose.yml` for single-backend runs and
  `docker-compose.multi.yml` for the multi-datasource demo.

### Known limitations
- HTTP backend mode is single-threaded and has caching disabled; use LSRP
  mode for production throughput.
- svgd does not terminate TLS — deploy behind a reverse proxy in production.
- The `lsrp/` directory is a git submodule; protocol changes belong upstream
  at [Pavelavl/lsrp](https://github.com/Pavelavl/lsrp), not in this repo.

[Unreleased]: https://github.com/Pavelavl/svgd/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Pavelavl/svgd/releases/tag/v0.1.0
