# Contributing to svgd

Thanks for your interest in svgd! This is a short, practical guide to getting
the project running and landing a change. The full project overview lives in
[`readme.md`](./readme.md) and [`CLAUDE.md`](./CLAUDE.md) — read those if you
need the bigger picture.

## Project layout (where things live)

svgd has three components; know which one you're touching before you start:

| Path | Component | What it is |
|------|-----------|------------|
| `src/` | **svgd** (backend) | C server that reads RRD files and renders SVG. Speaks LSRP or plain HTTP (`config.json` → `server.protocol`). |
| `gate/` | **svgd-gate** (HTTP gateway + web UI) | C HTTP server in `gate/main.c`, JWT-like auth in `gate/auth/`, browser UI in `gate/static/`. Routes to one or more backends. |
| `lsrp/` | **lsrp** (git submodule) | The binary wire protocol between gate and backend. **A separate project** — protocol changes go in [Pavelavl/lsrp](https://github.com/Pavelavl/lsrp), not here. |

Shared backend business logic lives in `src/handler.c` (used by both LSRP and
HTTP modes); HTTP parsing is in `src/http.c`. SVG rendering is **JavaScript**,
executed by the embedded Duktape engine — see `src/scripts/generate_svg.js`.

## Setup

**1. Clone with submodules** (lsrp is a submodule — this is required):

```bash
git clone --recurse-submodules https://github.com/Pavelavl/svgd.git
# or, for an existing clone:
git submodule update --init --recursive
```

**2. Install build dependencies** (Debian/Ubuntu names):

```bash
sudo apt update
sudo apt install librrd-dev duktape-dev libssl-dev gcc make jq
```

> Note: the README calls the JS engine package `libduktape-dev`, but on
> Debian/Ubuntu the actual package name is `duktape-dev`.

**3. Copy the example configs** (these are not committed):

```bash
cp config.sample.json config.json
cp auth.example.json gate/auth/auth.json   # only if you want auth
chmod 600 gate/auth/auth.json              # it contains secrets
```

You'll also need RRD data at `config.json` → `rrd.base_path` for anything to
show up. See the [collectd](https://github.com/Pavelavl/cpu-http-monitor) and
[rrdcached](./readme.md#rrdcached) sections of the README.

## Build & run

```bash
make build          # builds both bin/svgd and bin/svgd-gate
make build-backend  # backend only

./bin/svgd ./config.json                                 # backend on :8081
./bin/svgd-gate 127.0.0.1 8081 8080 ./gate/static        # gate on :8080
```

Then open http://localhost:8080. (`make run-backend` / `make run` wrap these.)

## Test

```bash
make test          # e2e + load (Go workspace under tests/)
make test-ui       # Python/Selenium UI tests
make test-bench    # svgd vs RRDtool vs Graphite (needs Docker)
```

Go tests are integration tests — they spawn the compiled binaries and drive
them over LSRP/HTTP. They require `REPO_ROOT` to locate the binaries:

```bash
REPO_ROOT="$(pwd)" bash -c 'cd tests && go test -v -run TestName ./internal/e2e/...'
```

E2E/load tests expect a populated RRD file at the path in `config.json` (and
some scenarios want a running `rrdcached`). To match CI exactly, run
`make docker-test`. See `.github/workflows/test.yml` for the bootstrap.

## Where to make your change

The codebase is data-driven and intentionally small. Prefer the lightest layer
that can carry your change:

- **Chart appearance / rendering** → edit `src/scripts/generate_svg.js`. This is
  the primary extensibility surface and runs in a per-thread Duktape context.
- **New metric** → add an entry to the `metrics[]` array in `config.json`
  (`endpoint` + `rrd_path`, optionally with `requires_param`, `transform_type`,
  `title`, etc.). No code change needed. See the README for worked examples.
- **Web UI** → `gate/static/` (plain HTML/JS/CSS).
- **Auth** → `gate/auth/` (HMAC-SHA256 tokens via OpenSSL).
- **C / protocol behavior** → only when the above can't carry the change.

## C style

- Doxygen-style `@file` / `@brief` headers at the top of each file; document
  public functions with a `@brief` and param/return notes.
- **Keep HTTP and LSRP request parsing separate.** They live in `src/http.c`
  and the LSRP code respectively, and both delegate business logic to the
  shared `src/handler.c` (`handler_process()`). Don't fork logic between the
  two transports — extend `handler.c` instead.
- Caching (RRD data in `src/rrd/cache.c`, JS contexts in `src/rrd/svg.c`) is
  only initialized in LSRP mode — respect the existing
  `protocol != "http"` guard in `main()` when touching cache lifecycle.

## Git & pull requests

1. **Branch off `master`** with a descriptive name (`feat/...`, `fix/...`).
2. **Write clear commit messages** — a short subject line, then a body that
   explains *why*. Keep commits focused; squash noisy WIP commits.
3. **Open a pull request** against `master`. Fill in the PR template.
4. **CI must pass.** Workflows in `.github/workflows/` build both binaries and
   run the e2e/load suites. If a test needs RRD data you can't produce, call
   that out in the PR so a maintainer can reproduce.
5. **Keep changes tight.** Don't reformat unrelated code or bump versions in
   the same PR. Don't edit `lsrp/` here — upstream it to the lsrp repo and bump
   the submodule pointer in a separate change.

## Reporting issues

Use the issue templates (`.github/ISSUE_TEMPLATE/`). For security-sensitive
reports, follow [`SECURITY.md`](./SECURITY.md) — **do not open a public issue.**
