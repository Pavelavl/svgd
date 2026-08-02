---
name: Bug report
about: Report something that's broken or behaving unexpectedly
title: "[bug] "
labels: ["bug"]
assignees: []
---

## Summary

<!-- One or two sentences: what's wrong? -->

## Environment

- **svgd version / commit:** <!-- e.g. `./bin/svgd --version`, `git describe`, or commit SHA -->
- **svgd-gate version / commit:**
- **Component affected:** <!-- backend (svgd) / gate (svgd-gate) / web UI / auth / other -->
- **OS / distribution:**
- **Architecture:** <!-- x86_64 / arm64 -->
- **How installed:**
  - [ ] Built from source (`make build`)
  - [ ] Docker (`make docker-up` / `docker-compose`)
  - [ ] Prebuilt binary / other
- **`server.protocol`:** <!-- lsrp (default) or http -->

## Configuration

<!-- A minimal config snippet that reproduces the issue. REDACT SECRETS —
     never paste `jwt_secret`, `password`, or the contents of auth.json.
     For metrics, include the relevant entry from metrics[]. -->

```json
{
  "server": { "...": "..." },
  "metrics": [ ]
}
```

- **Auth enabled?** <!-- yes / no -->
- **Running behind a reverse proxy?** <!-- yes (which?) / no -->

## Steps to reproduce

1.
2.
3.

## Expected behavior

<!-- What you expected to happen. -->

## Actual behavior

<!-- What happened instead. -->

## Logs

<!--
Paste relevant output (backend stdout/stderr, gate logs, browser console
errors, curl -i output). Use code blocks. Redact secrets, IPs, and paths you
don't want to share.
-->

```
```

## Additional context

<!-- RRD file/collectd setup if relevant, screenshots of the broken chart,
     anything else that helps. -->
