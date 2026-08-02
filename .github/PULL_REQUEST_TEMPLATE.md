<!-- Thanks! Please read CONTRIBUTING.md before opening the PR. -->

## Summary

<!-- One or two sentences: what does this change do, and why? -->

## Motivation

<!-- What problem does this solve? Link any related issues
     ("Fixes #123", "Refs #456"). -->

## What changed

<!-- Bullet list of the notable changes. Call out anything non-obvious. -->

- 
- 
- 

## Where this lands

<!-- Check the layer(s) this touches. Prefer the lightest layer that carries
     the change (JS/config before C). -->

- [ ] SVG rendering (`src/scripts/generate_svg.js`)
- [ ] Web UI (`gate/static/`)
- [ ] Metrics config (`config.json` / `config.sample.json`)
- [ ] Backend C (`src/`) — HTTP and LSRP parsing kept separate, shared logic in `handler.c`
- [ ] Gateway C (`gate/`)
- [ ] Auth (`gate/auth/`)
- [ ] Tests
- [ ] Docs (`readme.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `CHANGELOG.md`)
- [ ] CI / build (`makefile`, `.github/workflows/`) — **do not edit workflows without reason**
- [ ] `lsrp/` submodule — if this is a protocol change, it belongs in
      [Pavelavl/lsrp](https://github.com/Pavelavl/lsrp), not here

## Checklist

- [ ] Branch is based on `master`.
- [ ] Commits are focused and have clear messages (explain *why*).
- [ ] No unrelated reformatting or version bumps mixed in.
- [ ] `make build` succeeds (both `bin/svgd` and `bin/svgd-gate`).
- [ ] Tests pass locally where feasible (`make test`, `make test-ui`).
       <!-- Go tests need REPO_ROOT and a populated RRD file — see CONTRIBUTING.md -->
- [ ] If a public-facing change, `CHANGELOG.md` `[Unreleased]` section updated.
- [ ] No secrets, `auth.json`, or `*.rrd` data committed.
- [ ] If touching C: Doxygen `@file`/`@brief` headers added/updated, and HTTP
      vs LSRP parsing kept separate.

## Notes for reviewers

<!-- Anything they should pay attention to, tricky parts, screenshots of UI
     changes, or instructions to reproduce. -->
