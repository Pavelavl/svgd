# svgd Roadmap

This is the public roadmap for `svgd`. It tracks what is done, what is in
progress, and what is planned. Versions are preliminary — `svgd` does not yet
follow a strict release cadence; the authoritative source of changes between
releases is the `git log` on `master` (see also `CHANGELOG.md`).

The roadmap follows three themes, in priority order:

1. **Reduce friction** — make `svgd` runnable in one command, packaged, and
   embeddable into the standard observability stack.
2. **Self-contained stack** — remove the dependency on the stalled `collectd`
   project via the bundled `svgd-collect` collector.
3. **Grow as a universal lightweight visualizer** — decouple rendering from the
   RRD source so `svgd` can plot metrics from anywhere.

---

## Status legend

- :white_check_mark: **Done** — shipped on `master`. (Tagged `v0.1.0` items are in the release; newer items are on `master` awaiting the next release.)
- :construction: **In progress** — actively being worked on.
- :bulb: **Planned** — designed and intended, not yet started.

---

## :white_check_mark: Done

### Open-source foundation (v0.1.0)

- [x] MIT `LICENSE`; English-primary `readme.md` with badges, a "Why svgd?"
      highlights section, and benchmark tables; Russian mirror `readme.ru.md`.
- [x] Community files: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`,
      `CHANGELOG.md`; GitHub issue / PR templates under `.github/`.
- [x] `make install` (honors `PREFIX`/`DESTDIR`, non-clobbering config install)
      and systemd units for `svgd` and `svgd-gate` (`.infra/systemd/`).

### Grafana datasource compatibility

- [x] `svgd-gate` serves the simpod / classic-SimpleJson structured datasource
      contract at `/grafana/*` (`search`, `query`, `annotations`, connection
      check). Verified end-to-end with a real Grafana container. The gate is a
      thin forwarder; JSON assembly happens in the backend's existing Duktape
      engine — no new dependencies.

### svgd-collect — self-contained drop-in collector

- [x] Standalone C collector ([`Pavelavl/svgd-collect`](https://github.com/Pavelavl/svgd-collect)),
      now a submodule at `svgd-collect/`. Reads `/proc`/`/sys` and writes RRDs in
      the identical collectd layout, so `config.json` is unchanged (drop-in).
- [x] **11 readers**: cpu, load, uptime, memory, swap, interface, disk, df,
      processes, thermal, tcpconns. DS names/types/RRA verified against real
      collectd RRD output.
- [x] Optional **rrdcached routing** (`collect.json` → `rrdcached_addr`; RRD
      creation stays direct, hot-path updates routed, dead-daemon fallback),
      **MIN/MAX RRAs** alongside AVERAGE, reader **error logging**, and a
      testable reader **registry**.

### Prometheus `/metrics` exposition

- [x] `svgd-collect` exposes an opt-in `/metrics` endpoint in
      [text exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/)
      (set `metrics_addr` in `collect.json`). Plain-C-sockets HTTP listener on a
      dedicated thread; gauge/counter types derived from the collectd DS
      definitions; threadsafe two-buffer snapshot of the last collection cycle.
      Makes the stack visible to Prometheus / Grafana / any modern observability
      consumer. (Roadmap item P1-8.)

### One-command quickstart demo

- [x] `docker compose -f docker-compose.demo.yml up` (or `make demo`) yields a
      working dashboard on `:8080` with **no collectd, no rrdcached, no manual
      config**. An `rrd-init` one-shot regenerates fresh demo RRDs on every
      start, so charts never go stale. (Roadmap item P1-5.)

### Documentation site & roadmap

- [x] mkdocs-material site (`docs/`, `mkdocs.yml`) — landing, architecture,
      quickstart, installation, configuration, and an honest comparison vs
      Monitorix / Munin / Netdata; deployed to GitHub Pages via
      `.github/workflows/docs.yml`. (Roadmap items P2-12 / P2-13.)
- [x] Public `ROADMAP.md` (this file). (Roadmap item P2-14.)

### Release packaging

- [x] `make dist` source tarball (version from `git describe`; `lsrp` flattened
      so packagers need no `git submodule init`).
- [x] `.github/workflows/release.yml` — on `v*` semver tags, cross-compiles
      Linux **amd64 + arm64** binary packages + the source tarball and publishes
      a GitHub Release with CHANGELOG-derived notes. (Roadmap item P1-9.)
- [x] AUR `PKGBUILD` (`packaging/aur/svgd/PKGBUILD`) — builds from the release
      tarball to Arch-standard paths with a `sysusers.d` fragment. (Roadmap item
      P1-10.)

### Pluggable metric sources (Phase 2)

- [x] A `metric_source_t` seam at `rrd_fetch_data()` lets `svgd` read from RRD
      **and** live `/proc` **and** Prometheus text-exposition, selected
      per-metric via `config.json` → `source` (default `rrd`). Turns `svgd` from
      an "RRD viewer" into a universal lightweight visualizer. (Theme 3 / Roadmap
      item P2-12.)

### Visualization: themes & metric gallery

- [x] `light` / `dark` / `high-contrast` SVG render themes in
      `generate_svg.js`, selectable via `?theme=` or `server.theme`.
- [x] `docs/gallery.md` metric & theme gallery with `config.json` recipes for
      common scenarios and a custom-theme howto. (Roadmap item P3-16.)

### Test & quality

- [x] C unit-test harness (`tests/c/`, `make test-c`) for pure logic, wired as
      the first prerequisite of `make test`.
- [x] Standalone CI for the `svgd-collect` submodule (17 unit + 3 integration
      suites).

---

## :construction: In progress

- _(nothing major — v0.1.0 is cut and tagged; the post-release work above
  (pluggable reader, themes, gallery, C unit tests, svgd-collect CI) has landed
  on `master` and is awaiting the next release.)_

---

## :bulb: Planned

### Distribution extras

- [ ] **Homebrew tap** and **`.deb` / `.rpm`** packages (AUR + release tarballs
      already ship). Release **artifact signing** (`cosign` / GPG).

### Growth

- [ ] **PR campaign** — Show HN, r/selfhosted, r/homelab, Хабr (Russian
      audience), dev.to. Deliberately **last**: launched only after the release,
      quickstart, packaging, and docs are confirmed working for a stranger, so
      incoming traffic lands on something runnable. (Roadmap item P2-15.)

---

## Deliberately out of scope

These are intentionally **not** on the roadmap:

- A universal collectd replacement with 200+ plugins (a losing race against
  Telegraf / node_exporter / Netdata).
- Built-in ML-based anomaly detection or a Netdata-class alerting engine.
- A custom TSDB. RRD's bounded-size, fixed-resolution model is a deliberate fit
  for constrained disks; an own-format storage engine may be revisited later but
  is not planned for v1.
- Windows / macOS / BSD collection from `svgd-collect` on day one. `svgd` (the
  renderer) reads RRD files written by anything; `svgd-collect` (the collector)
  is Linux-only for now and will expand only on community demand.

---

## How to contribute

Issues and pull requests are welcome at
[github.com/Pavelavl/svgd](https://github.com/Pavelavl/svgd). See
`CONTRIBUTING.md` for setup and conventions. If you have a specific metric,
chart theme, or integration you would like to see, opening an issue first helps
us scope it against this roadmap.
