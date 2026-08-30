# OPERATION-rhel.md — running the stack on RHEL with SELinux enforcing

Companion to `OPERATION.md`, not a replacement. Read that file first for what the services
are and how they fit together; this one covers only what differs on a RHEL host where
SELinux is in enforcing mode.

`$REPO` below is your checkout on the server. Set it once per shell:

```bash
export REPO=/path/to/RingRPQ-demo     # the directory containing ports.conf
```

---

## Why this file exists

`deploy/install-units.sh` installs **system** units into `/etc/systemd/system`. On RHEL with
SELinux enforcing, those units cannot start, because systemd runs as `init_t` and the
binaries they execute live under `/home`, labelled `user_home_t`:

```
avc: denied { execute } for comm="(ring-rpq-worker)" name="ring-rpq-worker"
  scontext=system_u:system_r:init_t:s0
  tcontext=unconfined_u:object_r:user_home_t:s0
  tclass=file permissive=0
```

The unit fails with `status=203/EXEC`. This hits `ringrpq-backend.service` (execs `uvicorn`
inside a conda env under `$HOME`) and every `ringrpq-worker@` instance (execs
`Ring-RPQ/build/ring-rpq-worker`). `ringrpq-xpath.service` execs `/usr/bin/java`, which is
`bin_t` and permitted — but it then has to *read* a jar under `$HOME`, so it fails too.

Three fixes were considered and rejected:

| Approach | Why not |
|---|---|
| `audit2allow` policy module | The generated rule is `allow init_t user_home_t:file execute` — it lets *every* system service execute *any* file in *any* user's home. Unacceptable on a shared machine. |
| Relabel the repo to `bin_t` | Needs the conda env relabelled too, plus `init_t` search on `user_home_dir_t` (your home is `drwx------`). Ends in a custom policy module anyway. |
| `setenforce 0` | Machine-wide security regression on a host where SELinux is actively enforcing for other services. |
| Move everything to `/opt` | Viable, but conda envs carry absolute shebangs and don't relocate — it means a second Miniconda install under `/opt`. Keep as a fallback. |

**The approach here: user units.** `systemd --user` runs your services in your own
`unconfined_u:unconfined_r:unconfined_t` context, where executing from your own home is
ordinary. No relabelling, no policy module, no machine-wide change.

Confirm you are an unconfined user before continuing — if this does not say `unconfined_u`,
stop and use the `/opt` fallback instead:

```bash
id -Z            # unconfined_u:unconfined_r:unconfined_t:s0-s0:c0.c1023
```

---

## Services at a glance

Postgres stays a **system** service: it is dnf-installed, runs as the `postgres` user out of
`/var/lib/pgsql`, and is correctly labelled, so SELinux never objects. Everything else moves
to user units.

| # | Service | Manager | Unit | Control |
|---|---|---|---|---|
| 1 | PostgreSQL | system | `postgresql.service` | `sudo systemctl …` |
| 2 | XPathRingRPQ | **user** | `ringrpq-xpath.service` | `systemctl --user …` |
| 3 | Backend API | **user** | `ringrpq-backend.service` | `systemctl --user …` |
| 4 | Ring-RPQ worker(s) | **user** | `ringrpq-worker@<db>.service` | `systemctl --user …` |
| 5 | Frontend | — | none — built assets served by the backend | — |

Never run `systemctl --user` under `sudo`. It would address root's user manager, not yours.

---

## The one real compromise: Postgres ordering

A user unit cannot declare `After=postgresql.service`. Ordering dependencies do not cross
between the user manager and the system manager, so the `Wants=`/`After=postgresql.service`
lines in `deploy/systemd/*.service` have no user-unit equivalent — systemd silently ignores
ordering against a unit its manager cannot see.

This matters because the backend opens its connection pool at *import* time: started before
Postgres is accepting connections, it does not wait, it crashes.

Replaced below with an explicit readiness gate:

```ini
ExecStartPre=/usr/bin/pg_isready -q -h localhost -p 5432 || wait-loop
TimeoutStartSec=300
```

plus `Restart=on-failure` as a second line of defence. The gate is a real wait, not a fixed
sleep, so a slow Postgres start at boot is handled without guessing at a delay.

---

## One-time setup

Everything in `SETUP.md` must already be done — built binaries, built jar, conda env,
`backend/.env`, and a `postgresql.service` that starts.

### 1. Enable lingering

Without this your user manager exits when your last SSH session closes, taking every service
with it, and nothing starts at boot.

```bash
sudo loginctl enable-linger "$USER"
loginctl show-user "$USER" --property=Linger      # must print Linger=yes
```

### 2. Generate the user units

This mirrors what `deploy/install-units.sh` does — same discovery of `uvicorn`, `java`, the
ports, and the worker conninfo — but writes user units to `~/.config/systemd/user` instead of
`/etc/systemd/system`, and needs no `sudo`.

```bash
set -eu
cd "$REPO"
source "$REPO/ports.sh"          # exports RINGRPQ_BACKEND_PORT / RINGRPQ_XPATH_PORT

UNIT_DIR="$HOME/.config/systemd/user"
mkdir -p "$UNIT_DIR"

UVICORN="${UVICORN:-$HOME/miniconda3/envs/ringrpq-backend/bin/uvicorn}"
JAVA="${JAVA:-$(command -v java || true)}"
PGREADY="$(command -v pg_isready || true)"
BACKEND_HOST="${BACKEND_HOST:-127.0.0.1}"

# Workers speak libpq directly, so strip SQLAlchemy's "+psycopg2".
CONNINFO="$(grep -E '^DATABASE_URL=' "$REPO/backend/.env" 2>/dev/null | head -n1 | cut -d= -f2- | sed 's/+psycopg2//')"

for v in UVICORN JAVA PGREADY CONNINFO; do
    [ -n "${!v}" ] || { echo "could not determine $v" >&2; exit 1; }
done
[ -x "$UVICORN" ] || { echo "$UVICORN is not executable" >&2; exit 1; }

echo "repo=$REPO  uvicorn=$UVICORN  java=$JAVA"
echo "backend=$BACKEND_HOST:$RINGRPQ_BACKEND_PORT  xpath=:$RINGRPQ_XPATH_PORT"

cat > "$UNIT_DIR/ringrpq.target" <<EOF
# User-manager counterpart to deploy/systemd/ringrpq.target.
# No Wants=/After=postgresql.service: that is a system unit and the
# dependency cannot cross into the user manager. Each service gates on
# Postgres itself via ExecStartPre instead.
[Unit]
Description=RingRPQ demo stack (user units)

[Install]
WantedBy=default.target
EOF

cat > "$UNIT_DIR/ringrpq-xpath.service" <<EOF
[Unit]
Description=XPathRingRPQ translation service (Spring Boot)
PartOf=ringrpq.target

[Service]
Type=simple
WorkingDirectory=$REPO/XPathRingRPQ
Environment=RINGRPQ_XPATH_PORT=$RINGRPQ_XPATH_PORT
# The packaged jar, not mvnw spring-boot:run — that forks a child JVM
# systemd cannot track. Build first: ./mvnw -DskipTests package
ExecStart=$JAVA -jar $REPO/XPathRingRPQ/target/demo-0.0.1-SNAPSHOT.jar --server.port=$RINGRPQ_XPATH_PORT
Restart=on-failure
RestartSec=5

[Install]
WantedBy=ringrpq.target
EOF

cat > "$UNIT_DIR/ringrpq-backend.service" <<EOF
[Unit]
Description=RingRPQ backend API (FastAPI/uvicorn)
PartOf=ringrpq.target

[Service]
Type=simple
WorkingDirectory=$REPO/backend
Environment=RINGRPQ_XPATH_PORT=$RINGRPQ_XPATH_PORT
# Stands in for After=postgresql.service, which a user unit cannot express.
# The backend opens its pool at import time, so it must not start before
# Postgres accepts connections.
ExecStartPre=/bin/bash -c 'until $PGREADY -q -h localhost -p 5432; do sleep 2; done'
# Absolute path into the conda env — no 'conda activate', which needs an
# interactive shell systemd does not provide.
ExecStart=$UVICORN app.main:app --host $BACKEND_HOST --port $RINGRPQ_BACKEND_PORT
Restart=on-failure
RestartSec=5
TimeoutStartSec=300

[Install]
WantedBy=ringrpq.target
EOF

cat > "$UNIT_DIR/ringrpq-worker@.service" <<EOF
# Templated on database_name. Those contain '/', so instances must be
# addressed through systemd-escape; %I is the unescaped name, which is what
# ring-rpq-worker receives.
[Unit]
Description=Ring-RPQ query worker for %I
PartOf=ringrpq.target

[Service]
Type=simple
# ring-rpq-worker resolves ./data (or ./data-dir.conf) and the ./run/ pid
# file relative to its cwd, so this must be Ring-RPQ/.
WorkingDirectory=$REPO/Ring-RPQ
Environment=RING_RPQ_DB_CONNINFO=$CONNINFO
ExecStartPre=/bin/bash -c 'until $PGREADY -q -h localhost -p 5432; do sleep 2; done'
ExecStart=$REPO/Ring-RPQ/build/ring-rpq-worker %I
# SIGTERM lets the worker finish its claimed row and remove its pid file. A
# row killed mid-flight stays 'running' in query_cache with no auto-recovery,
# so give a large graph's query room to finish.
KillSignal=SIGTERM
TimeoutStopSec=120
TimeoutStartSec=300
Restart=on-failure
RestartSec=5

[Install]
WantedBy=ringrpq.target
EOF

systemctl --user daemon-reload
systemctl --user enable ringrpq.target ringrpq-backend.service ringrpq-xpath.service
echo "units written to $UNIT_DIR"
```

Re-run this whole block after editing `ports.conf` or `backend/.env` — the values are baked
in, exactly as `install-units.sh` bakes them into the system units.

### 3. Enable one worker per database

The user-manager counterpart to `deploy/enable-workers.sh`. It reuses the repo's own
discovery rule, so a database that appears in the UI is one that got a worker.

```bash
source "$REPO/Ring-RPQ/worker-discovery.sh"
discover_databases | while IFS= read -r db; do
    [ -n "$db" ] || continue
    unit="ringrpq-worker@$(systemd-escape "$db").service"
    if systemctl --user is-active --quiet "$unit"; then
        echo "  already running  $db"
    else
        systemctl --user enable --now "$unit" && echo "  started          $db"
    fi
done
```

Idempotent, and re-running it is the rescan: drop a new database's files under
`Ring-RPQ/data/`, run it again, and only the new database gets a unit.

### 4. Start everything

```bash
sudo systemctl start postgresql          # system unit
systemctl --user start ringrpq.target    # everything else
systemctl --user status 'ringrpq*' --no-pager
```

---

## Daily operations

| Task | Command |
|---|---|
| Start the stack | `systemctl --user start ringrpq.target` |
| Stop the stack | `systemctl --user stop ringrpq.target` |
| Restart one service | `systemctl --user restart ringrpq-backend` |
| Status of everything | `systemctl --user status 'ringrpq*' --no-pager` |
| Follow backend logs | `journalctl --user -u ringrpq-backend -f` |
| Follow one worker | `journalctl --user -u "ringrpq-worker@$(systemd-escape 'foo/foo.dat')" -f` |
| Worker states | see the discovery loop above with `is-active` |
| Postgres | `sudo systemctl {start,stop,status} postgresql` |

`PartOf=ringrpq.target` on every service means stopping the target stops them all, same as
the system-unit path.

---

## Ports

Unchanged from `OPERATION.md` — `ports.conf` at the repo root remains the single source, and
the generator above bakes both values into the units. After editing it, re-run the generator
block and then:

```bash
systemctl --user daemon-reload
systemctl --user restart ringrpq.target
```

**This host has no firewalld** (`firewall-cmd` is not installed), so nothing filters inbound
ports. `BACKEND_HOST` therefore defaults to `127.0.0.1` here and should stay there: setting
`0.0.0.0` exposes the API to anything that can route to this machine, with nothing in front
of it. Put a TLS-terminating reverse proxy in the path before binding publicly.

---

## Differences from OPERATION.md, in one place

- `sudo systemctl` → `systemctl --user` for services 2–4. Postgres stays `sudo systemctl`.
- `journalctl -u X` → `journalctl --user -u X`.
- `deploy/install-units.sh` and `deploy/enable-workers.sh` are **not used**. Their
  user-manager equivalents are the two shell blocks above.
- `User=` is absent from every unit — invalid in user units, which always run as you.
- `Wants=`/`After=postgresql.service` are absent — replaced by the `ExecStartPre` gate.
- `WantedBy=multi-user.target` → `WantedBy=default.target` in the target's `[Install]`.
- Lingering must be enabled, or nothing survives logout or starts at boot.

---

## Troubleshooting

**`Failed to connect to bus`** — no user D-Bus session. Check `echo $XDG_RUNTIME_DIR` is set
(`/run/user/<uid>`). If you reached this host with `sudo su - dxthki` rather than a real
login, reconnect over SSH directly.

**`status=203/EXEC` on a user unit** — a path in the unit is wrong or not executable, *not*
SELinux (your own processes may execute your own files). Check the `ExecStart` path exists
and has `+x`.

**Backend stuck in `activating (start-pre)`** — the `pg_isready` gate is still waiting;
Postgres is not accepting connections. Check `sudo systemctl status postgresql`. After
`TimeoutStartSec=300` it fails outright.

**Services vanish after logout** — lingering is not enabled. See step 1.

**Confirming SELinux is not the problem** — recent denials only:

```bash
sudo ausearch -m AVC -ts recent | grep -v mongod
```

**Falling back to `/opt`** — if you later need genuine system units (to run services as a
dedicated non-login account, say), move both the repo and a fresh Miniconda under `/opt`,
where files are labelled `usr_t` and `init_t` may execute them, then use `deploy/` unchanged.
