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

- :white_check_mark: **Done** — shipped on `master` (and in the `v0.1.0` tag).
- :construction: **In progress** — actively being worked on.
- :bulb: **Planned** — designed and intended, not yet started.

---

## :white_check_mark: Done

### Open-source foundation (v0.1.0)

- [x] MIT `LICENSE`.
- [x] English-primary `readme.md` with badges, a "Why svgd?" highlights section,
      and benchmark tables preserved verbatim; Russian mirror at `readme.ru.md`.
- [x] Community files: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`,
      `CHANGELOG.md`; GitHub issue / PR templates under `.github/`.
- [x] `make install` (honors `PREFIX`/`DESTDIR`, non-clobbering config install)
      and systemd units for `svgd` and `svgd-gate` (`.infra/systemd/`).
- [x] `.gitignore` whitelist for project docs; `duktape-dev` package name fixed
      in install instructions.

### Grafana datasource compatibility

- [x] `svgd-gate` serves the simpod / classic SimpleJson structured datasource
      contract at `/grafana/*` (`search`, `query`, `annotations`, connection
      check). Verified end-to-end with a real Grafana container. The gate is a
      thin forwarder; JSON assembly happens in the backend's existing Duktape
      engine — no new dependencies.

### svgd-collect — drop-in collector (Phase 1)

- [x] Standalone C collector ([`Pavelavl/svgd-collect`](https://github.com/Pavelavl/svgd-collect)),
      now a submodule at `svgd-collect/`. Reads `/proc`/`/sys` and writes RRDs
      in the identical collectd layout, so `config.json` is unchanged.
- [x] Type dictionary (16 collectd types), `metric_t` model, collectd-layout
      path builder, RRA builder (ported from collectd's `rra_get`), librrd
      writer, `cpu` reader (`/proc/stat` → `cpu-total/percent-active.rrd`).
- [x] Readers: load, uptime, memory, swap, interface, disk, df, processes +
      reader registry + multi-reader integration. DS names/types/RRA verified
      against real collectd RRD output.
- [x] Connected as submodule in `svgd`; README drop-in section + CHANGELOG
      updated.

---

## :construction: In progress

- [ ] **Push `master` to GitHub** (svgd itself is still local; `svgd-collect` is
      already on its remote) with the `v0.1.0` tag.
- [ ] **svgd-collect completion** — `thermal` + `tcpconns` readers (no reference
      RRDs in the current collectd layout, built from the `reader_t` template);
      rrdcached routing in the writer; `MIN`/`MAX` RRAs; reader-error logging;
      registry edge-case tests.
- [ ] **Quickstart demo** — a `docker-compose` with pre-populated RRD data so
      `docker compose up` yields a working dashboard on `:8080` with no manual
      collectd/rrdcached setup. (Roadmap item P1-5.)
- [ ] **This documentation site** (mkdocs + GitHub Pages) and the comparison
      page vs Monitorix / Munin / Netdata. (Roadmap items P2-12 / P2-13.)

---

## :bulb: Planned

### Ecosystem integration

- [ ] **Prometheus `/metrics` exposition** — emit the standard text exposition
      format from `svgd-collect` (or the gate), making the stack visible to
      Prometheus / Grafana / any modern observability consumer. (P1-8.)
- [ ] **Pluginable reader (Phase 2)** — introduce a `metric_source_t` seam at
      `src/rrd/reader.c:rrd_fetch_data()` (already a clean boundary) so `svgd`
      can read from RRD **and** live `/proc` **and** Prometheus text-exposition.
      Turns `svgd` from an "RRD viewer" into a universal lightweight
      visualizer. (P2-12.)
- [ ] **Metric gallery / recipes** — ready-made `config.json` snippets for
      common scenarios (Postgres, Nginx, a Docker host, a Raspberry Pi) and
      community chart themes via `generate_svg.js`. (P3-16.)

### Packaging & releases

- [ ] **GitHub Releases with semver tags** and a per-release changelog
      (benchmarks in the release notes). (P1-9.)
- [ ] **Distribution packages** — AUR (Arch, the author's native platform and
      CI target) plus release tarballs at minimum; Homebrew tap and `.deb`/`.rpm`
      as follow-ons. (P1-10.)

### Trust & growth

- [ ] **Public roadmap surfaced in-repo** (this file) and via GitHub Projects.
      (P2-14.)
- [ ] **PR campaign** — Show HN, r/selfhosted, r/homelab, Хабр (Russian
      audience), dev.to. Deliberately **last**: launched only after push,
      quickstart, packaging, and docs are in place, so incoming traffic lands on
      something runnable. (P2-15.)

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
