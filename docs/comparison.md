# Comparison

An honest side-by-side of `svgd` against the lightweight-monitoring field:
**Monitorix**, **Munin**, and **Netdata**. The goal is to help you decide when
`svgd` is the right tool — and, just as importantly, when it is **not**.

## TL;DR

`svgd` occupies a specific, defensible niche:

> **The most resource-efficient way to get beautiful system-metric charts — for
> edge / IoT / embedded / constrained devices, and for embedding into any
> interface via zero-dependency SVG.**

If your priority is maximum metric coverage, alerting, or ML analytics, use
Netdata or a Prometheus stack. If you want the lightest possible charts you can
embed anywhere, and you are willing to live with a smaller metric set to get
them, `svgd` is the tool.

## Comparison table

| | **svgd** | **Monitorix** | **Munin** | **Netdata** |
|---|---|---|---|---|
| **Language** | C + embedded JS | Perl | Perl | C |
| **Memory under load** | **~10 MB** | ~50–150 MB | varies (Perl + RRD) | ~100–400 MB+ |
| **CPU under load** | **~0%** (measured) | low | low | low–moderate (claims "minimal", de facto heavier) |
| **Throughput** | **2830 RPS** (c=50) | not a focus | not a focus | real-time, per-second |
| **Storage** | RRD (librrd) | RRD | RRD | own engine / RRD / DB |
| **Output format** | **Self-contained SVG per endpoint** | HTML + PNG/RRD graphs | HTML + PNG graphs | Web dashboard (JS) |
| **Embeddable as `<img>`/`<svg>`** | **Yes, from every endpoint** | No (rendered HTML pages) | No (rendered HTML pages) | No (JS dashboard) |
| **Extensibility model** | **Edit JS (`generate_svg.js`), no recompile** | Perl plugins / config | Perl plugins (500+) | C/Go/Python plugins |
| **Plugin / metric count** | ~11 metric families (config-driven) | moderate, built-in | **500+ plugins** | **800+ plugins / collectors** |
| **Alerting** | No (rendering only) | Yes (built-in) | Yes | Yes (rich) |
| **Multi-host** | Yes — one gate fronts N backends | Yes (remote groups) | Yes (master/node) | Yes (Netdata Cloud) |
| **First release** | 2026 | 2005 | 2002 | 2016 |
| **Maturity / community** | Young, small | Mature | Mature, large | Very large, active |
| **License** | MIT | GPL | GPL | GPL v3 |

Benchmarks are `svgd`'s own (see the project README); competitor resource
figures are order-of-magnitude community observations, not a like-for-like
benchmark run under this project's harness. Treat them as directional.

## Where `svgd` is stronger

### 1. Resource efficiency

This is `svgd`'s reason to exist. It is the only system in this group with a
measured **~0% CPU / ~10 MB RAM** footprint under load. The benchmark harness
(held in `tests/internal/comparison/`) reports `svgd` serving 2830 RPS while
Graphite used 70% CPU and 241 MB RAM on the same machine. Monitorix and Munin
are both "lightweight" by reputation, but neither is in the ~10 MB / ~0% CPU
class, and both carry the Perl runtime. Netdata markets itself as low-overhead
but in practice runs into the hundreds of MB once the full collector set is
active.

This matters on **Raspberry Pi, routers, NAS boxes, $2 VPS, embedded
controllers** — any target where a 200 MB agent is a non-starter.

### 2. Embeddable SVG output

Every `svgd` endpoint returns a self-contained SVG chart:

```html
<img src="http://pi:8080/cpu?period=3600" alt="cpu">
```

That is the entire integration. No iframe, no JS SDK, no bundler, no token in
the page. Monitorix, Munin, and Netdata all render dashboards as HTML+JS; you
cannot, in practice, drop a single metric chart into a README, a wiki, or an
external status page with one `<img>` tag. This is an under-served use case and
the most concrete differentiator.

### 3. Transparent JS extensibility (no recompile)

Chart appearance is a single JavaScript file: `src/scripts/generate_svg.js`,
run by the embedded Duktape engine. Change colors, scales, smoothing, axis
formatting, legends — save the file, restart, done. The C binary never
rebuilds. By contrast, Monitorix and Munin plugins are Perl; Netdata collectors
are C/Go/Python. Each has its own extensibility story, but none makes "change
how the chart looks" quite this close to "edit a script and reload."

## Where `svgd` is weaker

This section is here on purpose. `svgd` is not a general-purpose replacement
for any of the tools above.

- **Far fewer metrics out of the box.** ~11 metric families (cpu, memory, swap,
  load, uptime, disk, interface, filesystem, processes, network, thermal +
  whatever you wire up via RRD). Munin has 500+ plugins; Netdata has 800+.
  `svgd` does not try to compete on coverage.
- **No built-in alerting.** `svgd` renders charts. If you need thresholds,
  notifications, anomaly detection, or on-call routing, use Netdata or a
  Prometheus + Alertmanager stack. Alerting is on the roadmap but not the
  focus.
- **No Prometheus `/metrics` exposition (yet).** `svgd` speaks SVG and
  Grafana-JSON. A Prometheus text exposition endpoint is planned; until then,
  `svgd` is not a drop-in Prometheus exporter. (See the
  [Roadmap](roadmap.md).)
- **Smaller community and plugin base.** `svgd` is a young project. There is no
  plugin marketplace, no large gallery of community dashboards, and a small
  contributor base compared to Munin/Netdata.
- **Collection is Linux-only.** The `svgd-collect` collector reads `/proc` and
  `/sys`. BSD/macOS/Windows collection is not supported and not planned for v1.
  (You can still point `svgd` at RRD files written by anything, on any host.)
- **HTTP mode is a second-class citizen.** Caching and the thread pool exist
  only in LSRP mode. Plain HTTP mode is single-threaded and caching-disabled —
  fine for a quick look, not for production throughput.
- **No bundled long-term storage engine of its own.** `svgd` relies on RRD
  files (bounded size, fixed resolution) written by collectd or `svgd-collect`.
  This is a deliberate fit for constrained disks, but it is not a TSDB.

## Defensible niche — and what is deliberately out of scope

`svgd` does not aim to be "yet another Grafana" or a Telegraf replacement. The
roadmap is explicitly scoped to:

1. **Be the lightest rendering layer** for systems that cannot afford a heavier
   agent.
2. **Be the easiest chart output to embed** in any surface that renders HTML.
3. **Stay extensible in JavaScript** so appearance is decoupled from the C
   binary.

Out of scope (intentionally): a universal collector with 200 plugins, ML-based
anomaly detection, a full TSDB, or a Netdata-class alerting engine. Competing
there would dilute the one thing `svgd` does better than the alternatives.

## `svgd-collect` vs collectd

The bundled `svgd-collect` exists for a specific reason: collectd's last stable
release was 5.12.0 in **September 2021**, and 6.0 has been stuck in release
candidates for years
([issue #4186](https://github.com/collectd/collectd/issues/4186)). Depending
on a stalled project is a strategic risk.

`svgd-collect` is **not** a universal collectd replacement. It covers exactly
the metric families `svgd` renders — reading `/proc`/`/sys` and writing the
same RRD layout as collectd, so `config.json` works unchanged. It trades
collectd's ~200 plugins for a ~2k-line C footprint in the spirit of `svgd`'s
"extreme efficiency" brand.

| | **collectd** | **svgd-collect** |
|---|---|---|
| **Scope** | universal (200+ plugins) | exactly `svgd`'s 11 metric families |
| **Size** | very large C/C++ codebase | ~2k lines of C |
| **Status** | stalled (last stable 2021) | actively developed |
| **RRD layout** | canonical | **identical** (verified against real collectd output) |
| **Use it if** | you need its plugin breadth or have it deployed | you want a self-contained `svgd` stack with no collectd dependency |
