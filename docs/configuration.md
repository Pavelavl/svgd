# Configuration

`svgd` is configured by two files: `config.json` for the backend and
`datasources.json` for the gateway. Both are gitignored (copy from the
samples). This page is a reference for every field.

## `config.json` — backend

Copy `config.sample.json` to `config.json` and edit.

```json
{
  "server": {
    "tcp_port": 8081,
    "protocol": "lsrp",
    "allowed_ips": "127.0.0.1",
    "rrdcached_addr": "",
    "thread_pool_size": 4,
    "cache_ttl_seconds": 5,
    "verbose": 0,
    "theme": "light"
  },
  "rrd": {
    "base_path": "/opt/collectd/var/lib/collectd/rrd/localhost"
  },
  "js": {
    "script_path": "./scripts/generate_svg.js"
  },
  "metrics": [ /* see "Metrics array" below */ ]
}
```

### `server.*`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `tcp_port` | int | `8081` | TCP port the backend listens on (LSRP or HTTP). |
| `protocol` | string | `"lsrp"` | Transport: `"lsrp"` (binary, thread pool, caching) or `"http"` (single-threaded, caching disabled). |
| `allowed_ips` | string | `"127.0.0.1"` | Comma-separated allowlist of client IPs. |
| `rrdcached_addr` | string | `""` | rrdcached address — `unix:/path/to.sock` or `host:port`. Empty = direct file I/O. |
| `thread_pool_size` | int | `4` | Worker threads (LSRP mode only). |
| `cache_ttl_seconds` | int | `5` | TTL for cached RRD data (LSRP mode only). |
| `verbose` | int | `0` | Logging verbosity (`0` = quiet). |
| `theme` | string | `"light"` | SVG render theme: `"light"`, `"dark"`, or `"high-contrast"`. Overridden per-request by the `?theme=` query parameter. See [Gallery](gallery.md#themes). |

> **Production note:** HTTP mode (`"protocol": "http"`) is a compatibility
> fallback — single-threaded, no caching, no pre-warmed JS contexts. Use LSRP
> mode for any throughput-sensitive deployment.

### `rrd.*`

| Field | Type | Description |
|-------|------|-------------|
| `base_path` | string | Directory holding the RRD files. collectd and `svgd-collect` both write here. |

### `js.*`

| Field | Type | Description |
|-------|------|-------------|
| `script_path` | string | Path to the SVG-generation script (`generate_svg.js`). The default `config.json` uses `./scripts/generate_svg.js`; `make build` creates the `scripts` symlink to `src/scripts/`. |

## Metrics array

`metrics[]` is the heart of `svgd`'s data-driven design. Each entry maps a URL
endpoint to an RRD file and describes how to render it.

### Required fields

| Field | Description |
|-------|-------------|
| `endpoint` | URL path used to access the metric, e.g. `"cpu"` or `"ram/process"`. |
| `rrd_path` | Path to the RRD file, relative to `rrd.base_path`. May contain `%s` (see below). |

### Optional fields

| Field | Description |
|-------|-------------|
| `requires_param` | `true` if the endpoint expects a URL path segment substituted into `%s` in `rrd_path`. |
| `param_name` | Human-readable name of the parameter (shown in the UI), e.g. `"process_name"`. |
| `title` | Chart title. Supports `%s` for the parameter value. |
| `y_label` | Y-axis label. |
| `is_percentage` | `true` if the metric is 0–100% (affects rendering). |
| `transform_type` | Value transform: `"none"`, `"divide"`, `"sum"`, `"multiply"`. |
| `transform_divisor` | Divisor applied for `"divide"` (e.g. bytes → MB). |
| `value_format` | `printf`-style output format, e.g. `"%.1f"`. |

### URL path parameters (`%s`)

When `requires_param: true`, the `rrd_path` may contain exactly one `%s`. The
matching URL segment is substituted in. The last path component of the request
becomes the parameter.

```text
endpoint = "ram/process"
rrd_path = "processes-%s/ps_rss.rrd"

GET /ram/process/postgres
  →  rrd.base_path/processes-postgres/ps_rss.rrd
```

### Examples

**CPU — a simple percentage metric:**

```json
{
  "endpoint": "cpu",
  "rrd_path": "cpu-total/percent-active.rrd",
  "title": "CPU Utilization",
  "y_label": "Usage (%)",
  "is_percentage": true,
  "value_format": "%.1f"
}
```

**Process memory — parameterized, with a divide transform (bytes → MB):**

```json
{
  "endpoint": "ram/process",
  "rrd_path": "processes-%s/ps_rss.rrd",
  "requires_param": true,
  "param_name": "process_name",
  "title": "Memory Usage",
  "y_label": "Memory (MB)",
  "transform_type": "divide",
  "transform_divisor": 1048576,
  "value_format": "%.1f"
}
```

**Network traffic — bytes → Mbit/s:**

```json
{
  "endpoint": "network",
  "rrd_path": "interface-%s/if_octets.rrd",
  "requires_param": true,
  "param_name": "interface",
  "title": "Network Traffic",
  "y_label": "Traffic (Mbit/s)",
  "transform_type": "divide",
  "transform_divisor": 125000,
  "value_format": "%.2f"
}
```

**PostgreSQL — a fixed-path metric:**

```json
{
  "endpoint": "postgresql/connections",
  "rrd_path": "postgresql-test/pg_numbackends.rrd",
  "title": "PostgreSQL Connections",
  "y_label": "Connections",
  "value_format": "%d"
}
```

## `datasources.json` — gateway

The gate fronts one or more backends. On first run it auto-creates
`datasources.json` from CLI arguments; thereafter you can edit it (or use the
`/_datasources` CRUD API) at runtime.

The active backend for a request is chosen from the `?datasource=` query
parameter, falling back to the configured default. This is how
`docker-compose.multi.yml` runs one gate in front of two backends.

## Auth (optional)

Copy `gate/auth/auth.example.json` to `gate/auth/auth.json`:

```json
{
  "password": "your_strong_password",
  "jwt_secret": "random_secret_at_least_32_characters",
  "token_expiry_days": 7
}
```

- `password` — used to obtain a token via `POST /_auth/login`.
- `jwt_secret` — HMAC-SHA256 signing key (minimum 32 characters).
- `token_expiry_days` — token lifetime (default 7).

When `auth.json` is absent, protected API endpoints return `401`. Static files
and `/_auth/*` are always public. Set `chmod 600 gate/auth/auth.json`;
`auth.json` is gitignored and must not be committed.

## Grafana datasource

`svgd-gate` implements the [simpod / classic
SimpleJson](https://github.com/grafana/simple-json-datasource) structured
datasource wire protocol at `/grafana/*`, so any Grafana instance can plot
`svgd` metrics as time-series with zero per-panel configuration.

| Method + path | Purpose |
|---|---|
| `GET /grafana` | "Save & Test" connection check → `200 {}` |
| `POST /grafana/search` | metric enumeration → `["cpu","ram",...]` |
| `POST /grafana/query` | time-series fetch → `[{"target":"cpu","datapoints":[[value, epoch_ms], ...]}]` |
| `POST /grafana/annotations` | stub → `[]` |

**Configure in Grafana:**

1. Install a compatible plugin: simpod `JSON Datasource`, or the classic
   `grafana-simple-json-datasource`.
2. Add a datasource of that type:
   - **URL**: `http://<svgd-gate-host>:8080/grafana`
   - **Access**: `Server` (Grafana proxies the request and injects auth headers).
   - **Custom HTTP Header**: `Authorization: Bearer <token>` from `svgd-gate`'s
     auth.
3. **Save & Test**. In a panel, pick the **Timeseries** query type and select a
   metric from the dropdown. Multi-series metrics (per-disk, per-interface…)
   return one target each.

**v1 limits:** the requested window width (`to − from`) is fetched as "last N
seconds up to now" — relative ranges work, absolute historical windows are
rounded to the width and fetched to now. Timestamps are converted from epoch
seconds to epoch milliseconds. Very large `/query` bodies are rejected with
`413` (the LSRP param channel is capped). Pass `?datasource=<name>` to target a
specific backend.
