# SVGD Installation Notes

Post-install setup. Run these after `make install` (or after installing a
packaged build). `make install` honors `DESTDIR` and `PREFIX` (default
`PREFIX=/usr/local`); the commands below assume the default layout.

## Layout installed by `make install`

| What                | Path                                            |
|---------------------|-------------------------------------------------|
| Backend binary      | `$(PREFIX)/bin/svgd`                            |
| Gate binary         | `$(PREFIX)/bin/svgd-gate`                       |
| Web UI (static)     | `$(PREFIX)/share/svgd/static/`                  |
| JS rendering script | `$(PREFIX)/share/svgd/scripts/generate_svg.js`  |
| Systemd units       | `$(PREFIX)/lib/systemd/system/svgd*.service`    |
| Config (if absent)  | `/etc/svgd/{config,datasources,auth}.json`      |
| Runtime dirs        | `/var/lib/svgd`, `/var/log/svgd`, `/etc/svgd`   |

> Config files under `/etc/svgd/` are installed **only if they don't already
> exist** — re-running `make install` never overwrites them. To regenerate
> from the samples, delete the file first or copy from `*.sample.json` /
> `auth.example.json` at the repo root.

> The systemd unit files bake in the default `/usr/local/...` paths. If you
> installed with a non-default `PREFIX` (e.g. `make install PREFIX=/opt/svgd`),
> edit the `ExecStart=` lines in `svgd.service` / `svgd-gate.service`
> accordingly, or rebuild/reinstall with the default `PREFIX`.

## 1. Create the service user

The units run as an unprivileged `svgd` account:

```sh
useradd --system --no-create-home --shell /usr/sbin/nologin svgd
```

## 2. Edit configuration

Edit `/etc/svgd/config.json`:

- `rrd.base_path` — your collectd RRD directory, e.g. `/var/lib/collectd/rrd`.
- `server.tcp_port` — backend port (default `8081`); must match the 2nd arg
  of `ExecStart` in `svgd-gate.service`.
- `server.protocol`, `server.thread_pool_size`, `server.cache_ttl_seconds` —
  tune as needed.
- `metrics[]` — map endpoints to RRD paths to match your collectd layout.

Review `/etc/svgd/datasources.json` (the gate's backend list) and
`/etc/svgd/auth.json` (HMAC secrets; required if auth is enabled — see
`auth.example.json` for the schema).

## 3. Set config permissions

The gate reads `auth.json` while running as the `svgd` user. After creating
the user, make the configs group-readable by it:

```sh
chown -R root:svgd /etc/svgd
chmod 0640 /etc/svgd/auth.json
chmod 0644 /etc/svgd/config.json /etc/svgd/datasources.json
```

## 4. Reload systemd and enable services

```sh
systemctl daemon-reload
systemctl enable --now svgd svgd-gate
```

## 5. Open the HTTP port

The gate listens on `8080/tcp` by default. Open it in your firewall:

```sh
# firewalld
firewall-cmd --add-port=8080/tcp --permanent && firewall-cmd --reload
# ufw
ufw allow 8080/tcp
```

## 6. Verify

```sh
systemctl status svgd svgd-gate
journalctl -u svgd -u svgd-gate --since "5 min ago"
curl -fsS http://127.0.0.1:8080/   # gate web UI
```

## Notes

- **Logs:** services log to journald (`journalctl -u svgd`, `journalctl -u
  svgd-gate`). `/var/log/svgd` exists for any future file-based logging.
- **rrdcached:** if the backend uses `server.rrdcached_addr` pointing at a
  UNIX socket, socket `connect()` works under the units' `ProtectSystem=strict`
  on most distros; if your socket path needs write access, append its parent
  dir to `ReadWritePaths=` in `svgd.service` (see the comment in the unit).
- **Removing:** `systemctl disable --now svgd svgd-gate`, then remove the
  installed files and the `svgd` user (`userdel svgd`) if no longer needed.
