# Security Policy

## Reporting a vulnerability

**Do not open a public GitHub issue for security problems.**

If you believe you have found a security vulnerability in svgd, please report it
privately so it can be triaged and fixed before public disclosure:

1. Use **GitHub's private vulnerability reporting** — the
   ["Report a vulnerability"][gh-advisory] button on the
   **Security** tab of [Pavelavl/svgd](https://github.com/Pavelavl/svgd).
   This is the preferred channel.
2. Alternatively, contact the maintainer privately via GitHub
   ([Pavelavl](https://github.com/Pavelavl)).

Please include as much of the following as you can:

- A description of the issue and its potential impact.
- The component affected (`svgd` backend, `svgd-gate`, auth, the web UI, or the
  LSRP protocol — note that LSRP lives in a [separate repo](https://github.com/Pavelavl/lsrp)).
- Affected version / commit, OS, and how you're running it (built from source,
  Docker, etc.).
- Steps to reproduce, including any config (redact secrets).
- Suggested fix or mitigation, if you have one.

You should receive an acknowledgment within a few days. We will coordinate a
fix and disclosure timeline with you. Please avoid public discussion of the
issue until a fix is available.

[gh-advisory]: https://github.com/Pavelavl/svgd/security/advisories/new

## Supported versions

Only the **`master` branch / latest release** receives security fixes. There
are no separate maintenance lines. Please confirm the issue reproduces on the
current `master` before reporting.

| Version | Supported |
|---------|-----------|
| `master` / latest release | Yes |
| Older commits / tagged releases | No |

## Authentication model (please read before deploying)

svgd-gate ships with **optional JWT-like authentication** (HMAC-SHA256 tokens
via OpenSSL), configured by `gate/auth/auth.json`:

- Auth is **off by default**. If `auth.json` is absent, protected API endpoints
  return **401** (the UI still loads). To enable auth, copy
  `auth.example.json` → `gate/auth/auth.json`.
- Static files (HTML/JS/CSS) and the `/_auth/*` endpoints are always public.
- Tokens are signed with `jwt_secret` from `auth.json` and stored in the
  browser's `localStorage`. Token lifetime is `token_expiry_days` (default 7).

**Hardening recommendations for production:**

- `auth.json` contains secrets and **must not be committed**. Restrict its
  permissions: `chmod 600 gate/auth/auth.json`.
- Use a strong, randomly generated `jwt_secret` (at least 32 characters) and a
  strong `password`.
- **Put svgd-gate behind a reverse proxy that terminates HTTPS/TLS.** svgd
  itself speaks plain HTTP/LSRP and does not encrypt traffic. A TLS-terminating
  proxy (nginx, Caddy, Traefik, etc.) is the expected production deployment —
  otherwise tokens and data travel in cleartext.
- The backend listens on `server.tcp_port` (default 8081). Bind it to
  `127.0.0.1` or a private network and let the gate be the only thing that
  talks to it. `server.allowed_ips` (default `127.0.0.1`) restricts which IPs
  the backend accepts LSRP/HTTP connections from — keep that locked down.

## Scope

In scope: the `svgd` backend, `svgd-gate`, its web UI, and the configuration
files in this repository.

Out of scope (but welcome to report via the upstream projects):

- The **LSRP** protocol implementation — maintained at
  [Pavelavl/lsrp](https://github.com/Pavelavl/lsrp).
- **collectd** and **RRDtool** — third-party projects.

Reports about self-inflicted secrets exposure (e.g. publishing your own
`auth.json` or running a default password on a public port) are not considered
svgd vulnerabilities.
