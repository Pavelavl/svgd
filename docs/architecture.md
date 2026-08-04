# Architecture

`svgd` is a three-component monitoring system built around a deliberately small
C core, with chart rendering delegated to an embedded JavaScript engine.

## Request flow

```text
Browser ──HTTP──> svgd-gate (:8080) ──LSRP──> svgd backend (:8081)
                   │  JWT-like auth              │
                   │  static web UI              │  handler_process()
                   │  datasource routing          │   ├─ rrd_cache   (TTL hash table)
                   └── multi-datasource:          │   ├─ rrd_fetch_data()  [librrd]
                       one gate → N backends       │   └─ svg_generate()  [Duktape JS engine]
```

A browser hits `svgd-gate` over plain HTTP. The gate authenticates the request,
selects a backend by `?datasource=` (or the configured default), and forwards
the metric request over the binary LSRP protocol. The backend resolves the
endpoint to an RRD file, fetches the time-series via librrd, and hands the data
to a per-thread Duktape JS context that runs `generate_svg.js` to produce the
SVG. The SVG flows back through the gate to the browser.

## Components

### `svgd` — the backend (`src/`)

Reads RRD time-series files and renders SVG. The same business logic core —
`handler_process()` in `src/handler.c` — is shared by both transport modes:

- **LSRP mode** (`server.protocol: "lsrp"`, the default): binary wire protocol
  over TCP, a thread pool, and two caches (RRD data and JS contexts). This is
  the high-throughput production path.
- **HTTP mode** (`server.protocol: "http"`): single-threaded plain-HTTP mode.
  Since v0.2.0 it has **full cache parity** with LSRP — the RRD data cache and
  JS context pre-warm are initialized the same way. The only difference is
  concurrency (single-threaded vs. thread pool).

**Two caches** live in `src/rrd/`:

| Cache | File | Purpose |
|-------|------|---------|
| RRD data | `cache.c` | TTL-based hash table keyed by RRD path + period. Clones `MetricData` for thread safety. |
| JS contexts | `svg.c` | Per-thread Duktape contexts, pre-warmed at startup, so each request skips engine init. |

Both are initialized in **both modes** (since v0.2.0 — formerly HTTP skipped them).

### `svgd-gate` — the HTTP gateway (`gate/`)

The user-facing process. Responsibilities:

- Serves the browser UI from `gate/static/` (dashboard, login, themes, export).
- Authenticates API requests with optional JWT-like tokens (HMAC-SHA256 via
  OpenSSL). Static files and `/_auth/*` are always public.
- Routes requests to one or many backends via **multi-datasource routing**.
  Datasources are loaded from `datasources.json` and managed at runtime through
  the `/_datasources` CRUD API.
- Implements the Grafana SimpleJson-compatible datasource at `/grafana/*` as a
  thin forwarder — the backend does the JSON assembly via its existing Duktape
  engine, so no extra dependencies.

### `lsrp/` — the wire protocol (submodule)

[LSRP](https://github.com/Pavelavl/lsrp) (Lightweight Simple Request Protocol)
is the binary request/response format used between gate and backend in LSRP
mode. It is a separate project — protocol changes belong upstream, not in this
repo. The Go test client lives at
[`github.com/Pavelavl/go-lsrp`](https://github.com/Pavelavl/go-lsrp).

### Data sources (external to `svgd` itself)

`svgd` reads RRD files. They are written by either:

- **collectd** (external), or
- **svgd-collect** (bundled submodule) — a drop-in C collector that reads
  `/proc`/`/sys` and writes the same RRD layout, so no `config.json` change is
  needed.

See [Installation](install.md#data-sources).

## SVG rendering is JavaScript, not C

This is the single most important architectural decision to understand:

> Chart appearance and behavior are defined in JavaScript. The C code only
> moves data.

`src/rrd/svg.c` embeds the [Duktape](https://duktape.org/) engine and runs
`src/scripts/generate_svg.js`, passing it serialized `MetricData`. The JS
returns the SVG string. Consequences:

- **To change how charts look, edit the JS** — colors, scales, axes, legends,
  smoothing, formatting. No C recompile, no rebuild of the binary.
- **Themes (`light` / `dark` / `high-contrast`) are pure JS palettes**, selected
  per-request via `?theme=` or globally via `server.theme`. Adding a new one is a
  few lines in `THEMES` — the C side is agnostic. See the [Gallery](gallery.md).
- JS contexts are **per-thread and pre-warmed** at startup, so rendering is
  not bottlenecked by engine initialization on the hot path.
- The JS layer is the primary extensibility surface. Prefer extending it (and
  `config.json`) over touching C.

## Data-driven metrics

Metrics are **not hardcoded**. `config.json` → `metrics[]` is an array that maps
an `endpoint` (the URL path) to an `rrd_path` (relative to `rrd.base_path`):

```json
{
  "endpoint": "ram/process",
  "rrd_path": "processes-%s/ps_rss.rrd",
  "requires_param": true,
  "param_name": "process_name",
  "transform_type": "divide",
  "transform_divisor": 1048576
}
```

The `%s` in `rrd_path` is substituted from a URL path segment
(`GET /ram/process/postgres` → `processes-postgres/ps_rss.rrd`) when
`requires_param: true`. Parsed in `src/cfg.c`, applied in `src/handler.c`
(`build_rrd_path`, `extract_param_from_path`). See
[Configuration](configuration.md) for the full schema.

## Why it is fast

- **`select_optimal_step()`** in `src/rrd/reader.c` chooses the RRA/step that
  avoids over- or under-sampling for the requested period — real engineering,
  not just "fetch everything."
- **Two-tier caching**: the RRD cache avoids re-reading disk on repeated
  requests; the JS-context cache avoids re-initializing Duktape per request.
- **Thread pool + pre-warmed contexts** in LSRP mode give near-linear throughput
  scaling (1347 → 2830 RPS from c=1 to c=50) with a flat ~10 MB footprint.
- **No heavy runtime**: no Python, no Node, no JVM. Native C + a tiny embedded
  JS engine.

For measured numbers, see the benchmarks section of the
[project README](https://github.com/Pavelavl/svgd#performance).

## Security model

- Auth is **optional**. When `gate/auth/auth.json` is absent, protected API
  endpoints return `401`; static files and `/_auth/*` remain public.
- Tokens are JWT-like (HMAC-SHA256), issued by `POST /_auth/login` against a
  configured password, sent by the client as `Authorization: Bearer <token>`.
- `svgd` does **not** terminate TLS — deploy it behind a reverse proxy in
  production.
- `server.allowed_ips` constrains which clients may talk to the backend
  (default `127.0.0.1`).
