# OPERATION.md — running the stack

Day-to-day start/stop, assuming everything is already built (see `SETUP.md` for first-time
install/build). Native processes on Linux, no Docker.

There are **two ways to run this**, and you should pick one per machine:

- **Dev loop** — foreground processes plus `Ring-RPQ/run-workers.sh`. Fast to restart, logs in
  your terminal, nothing survives a reboot. Use this while working on the code.
- **Server** — systemd units from `deploy/`. Starts on boot, restarts on crash, logs to the
  journal. Use this on a deployed box.

`$REPO` below is your checkout, e.g. `~/Projects/RingRPQ-demo`.

## Services at a glance

| # | Service | Tech | Host:Port | Dev | systemd unit |
|---|---|---|---|---|---|
| 1 | PostgreSQL | Postgres 18 | `localhost:5432` | `systemctl` | `postgresql.service` |
| 2 | XPathRingRPQ | Java 17 / Spring Boot | `localhost:8080` † | `./mvnw spring-boot:run` | `ringrpq-xpath.service` |
| 3 | Backend API | Python / FastAPI + uvicorn | `localhost:8000` † | `uvicorn` | `ringrpq-backend.service` |
| 4 | Ring-RPQ worker(s) | C++ daemons | no port — poll Postgres | `./run-workers.sh` | `ringrpq-worker@<db>.service` |
| 5 | Frontend | React + Vite | `localhost:5173` | `npm run dev` | none — served by the backend in production |

† Default; both are set in `ports.conf` — see [Ports](#ports) below. The rest of this
document writes them out as 8000 and 8080 for readability.

Dependency order: Postgres first. The backend needs Postgres + XPathRingRPQ. The workers need
Postgres. The frontend needs the backend.

In dev, the browser goes to **http://localhost:5173**. In production there is no Vite process —
the backend serves the built UI and the API from **http://\<host\>:8000** (one origin, so
moving that port moves the whole UI with it).

---

## Ports

Both HTTP ports are set in **`ports.conf`** at the repo root, so a host where 8000 or 8080 is
already taken needs one file changed:

```ini
RINGRPQ_BACKEND_PORT=8000    # uvicorn: the API, and the built UI in production
RINGRPQ_XPATH_PORT=8080      # XPathRingRPQ's Spring Boot server
```

Everything derives from there rather than repeating the number:

| Consumer | How it reads the port |
|---|---|
| `uvicorn` (dev) | `ports.sh` exports `$RINGRPQ_BACKEND_PORT`; pass it to `--port` |
| `ringrpq-backend.service` | `install-units.sh` bakes it into `--port` |
| XPathRingRPQ (dev) | `application.properties`: `server.port=${RINGRPQ_XPATH_PORT:8080}` |
| `ringrpq-xpath.service` | `install-units.sh` bakes it into `--server.port` on the JVM command line |
| Backend → XPathRingRPQ | `app/config.py` parses `ports.conf` to build `XPATHRINGRPQ_URL` |
| Frontend dev server | `vite.config.js` parses `ports.conf` for its API base URL |

Precedence everywhere is **environment variable → `ports.conf` → built-in default**, so a
one-off run can override without editing the file:

```bash
RINGRPQ_BACKEND_PORT=9000 uvicorn app.main:app --reload --port 9000
```

In a shell, `source $REPO/ports.sh` exports both variables (and rejects a value that isn't a
port number, instead of leaving you to decode a bind failure).

**The systemd units hold a baked-in copy**, so changing `ports.conf` on a server means:

```bash
sudo -E ./deploy/install-units.sh && sudo systemctl restart ringrpq.target
```

That is all — no rebuild of anything. The frontend needs none either: production is
same-origin, so moving the backend's port moves the UI with it.

**The one trap is running the packaged jar by hand.** `application.properties` is copied into
`BOOT-INF/classes` at package time, so a jar built before a port change keeps serving the old
port however `RINGRPQ_XPATH_PORT` is set — and starts cleanly, with nothing in the log saying
why. `ringrpq-xpath.service` sidesteps this by passing `--server.port` as a command-line
argument (the highest-precedence source in Spring's property order, and one that isn't inside
the jar). By hand, do the same, or repackage first:

```bash
java -jar target/demo-0.0.1-SNAPSHOT.jar --server.port=$RINGRPQ_XPATH_PORT
./mvnw -DskipTests package     # or this, then the plain java -jar
```

`./mvnw spring-boot:run` is unaffected — it re-processes the file on every run.

Two ports are deliberately *not* here: Postgres's, which belongs to `DATABASE_URL` in
`backend/.env` alongside the credentials, and Vite's dev port, which Vite picks itself
(5173, then 5174, …) and which only matters in dev — `ALLOW_LOCALHOST_ORIGINS=true` in
`backend/.env` is what keeps CORS working when it walks.

---

## Preparing the environment

Needed on a fresh machine, and again whenever you pull changes touching a component's
dependencies or compiled sources. Full install gotchas (the SDSL fork's two required env vars,
libpqxx static linking, the WSL `node` trap) are in `SETUP.md` — this is just what to re-run.

### Python backend (conda)
```bash
cd $REPO/backend
conda activate ringrpq-backend
pip install -r requirements.txt      # after requirements.txt changes
alembic upgrade head                 # after a new migration
```
No build step — uvicorn runs the source, and `--reload` picks up edits live.

### C++ (Ring-RPQ)
```bash
cd $REPO/Ring-RPQ/build
cmake .. && make -j"$(nproc)"        # or: make ring-rpq-worker
```
Fresh clone: `mkdir -p build && cd build && cmake .. && make -j"$(nproc)"`.
Rebuild after any change under `Ring-RPQ/`, including `nfa/` and `CMakeLists.txt`.
**Restart the workers afterwards** — each holds its index in memory and won't pick up a new
binary on its own.

### Java (XPathRingRPQ)
```bash
cd $REPO/XPathRingRPQ
./mvnw -DskipTests package           # only needed for the systemd unit, which runs the jar
```
In dev, `./mvnw spring-boot:run` compiles and runs in one shot.

### Frontend (npm)
```bash
cd $REPO/frontend
npm install                          # after package.json / package-lock.json changes
python build_and_copy.py             # production only: builds and copies to $REPO/static/
```

---

## Dev loop

### 1. PostgreSQL
```bash
sudo systemctl start postgresql
pg_isready -h localhost -p 5432
```
Enable it permanently with `sudo systemctl enable --now postgresql`.

### 2. XPathRingRPQ
```bash
source $REPO/ports.sh                 # exports RINGRPQ_XPATH_PORT from ports.conf
cd $REPO/XPathRingRPQ && ./mvnw spring-boot:run
curl "http://localhost:$RINGRPQ_XPATH_PORT/translate?query=%3Fx%20%3Chas%3E%20%3Fy"
```
Without the `source`, Spring falls back to the 8080 in `application.properties`.

### 3. Backend API
```bash
source $REPO/ports.sh                 # exports RINGRPQ_BACKEND_PORT from ports.conf
cd $REPO/backend && conda activate ringrpq-backend
uvicorn app.main:app --reload --port $RINGRPQ_BACKEND_PORT
curl http://localhost:$RINGRPQ_BACKEND_PORT/health
```
Docs at `/docs`. Config in `backend/.env`; ports in `ports.conf`. The backend finds
XPathRingRPQ from `ports.conf` on its own — the `source` above is only for `--port`, which
uvicorn takes on the command line.

### 4. Ring-RPQ worker(s)
Each worker holds exactly one graph's index in memory (loaded once at startup, reused for every
query — no reloading, no subprocess, no temp files) and only claims `query_cache` rows matching
the `database_name` it was started with. Serving N databases means N instances.
`run-workers.sh` discovers every usable database and starts one each, backgrounded:

```bash
cd $REPO/Ring-RPQ
./run-workers.sh              # or: ./run-workers.sh start
./run-workers.sh status       # RUNNING/STOPPED per database, with pid
./run-workers.sh stop         # stops every worker it started
```

Logs and pid files land in `Ring-RPQ/run/<database>.log` / `.pid` (gitignored). The script is
location-independent and **idempotent** — re-running it only starts workers for databases that
don't already have a live one; anything already running, and whatever row it's mid-query on, is
left alone. That re-run *is* the rescan: drop a new database's files under `data/`, run it
again, and only the new database gets a worker.

Each instance polls once a second. A database with no worker running leaves its queries
`pending` forever — though any running worker opportunistically expires timed-out `pending`
rows for *every* database, so such a query still ages out rather than hanging.

Connection string: `$RING_RPQ_DB_CONNINFO`, else a second argument, else the `backend/.env`
credentials. Note it's a plain libpq URI (`postgresql://…`), **not** SQLAlchemy's
`postgresql+psycopg2://` form.

### 5. Frontend
```bash
cd $REPO/frontend && npm run dev
```

### All at once, one terminal
```bash
source $REPO/ports.sh
sudo systemctl start postgresql
cd $REPO/XPathRingRPQ && ./mvnw spring-boot:run < /dev/null > /tmp/xpathringrpq.log 2>&1 &
cd $REPO/backend && conda activate ringrpq-backend && \
  uvicorn app.main:app --reload --port $RINGRPQ_BACKEND_PORT < /dev/null > /tmp/backend.log 2>&1 &
$REPO/Ring-RPQ/run-workers.sh
cd $REPO/frontend && npm run dev < /dev/null > /tmp/frontend.log 2>&1 &
```

The `< /dev/null` matters most for the frontend. Vite puts stdin into raw mode to listen for
keypresses (`r`/`o`/`q`) whenever stdin is a TTY. A backgrounded job that still owns the
terminal's stdin gets `SIGTTIN`'d the moment Vite touches it, which **stops** the process
(`STAT=T` in `ps`) rather than crashing it. A stopped process ignores `SIGTERM` — signals other
than `SIGKILL`/`SIGCONT` stay pending until it resumes — so a later `pkill -f vite` silently
no-ops, the port stays held, and the next `npm run dev` walks to 5174, 5175, … Redirecting
stdin away from the TTY avoids this entirely. (This is POSIX job control, not a Linux quirk;
it behaves the same on macOS.)

### Stopping
```bash
pkill -9 -f "vite"                    # -9: a SIGTTIN-stopped Vite ignores SIGTERM (above)
$REPO/Ring-RPQ/run-workers.sh stop    # SIGTERM; each finishes its claimed row and exits
pkill -f "uvicorn app.main:app"
pkill -f "demo-0.0.1-SNAPSHOT.jar"    # or: pkill -f spring-boot:run
sudo systemctl stop postgresql        # usually just leave it running
```

Killing a worker *hard* mid-row leaves that row stuck in `status='running'` with no
auto-recovery; reset it by hand in `query_cache` if it happens. `run-workers.sh stop` uses
SIGTERM and is the safe path.

To find a stray process holding a port:
```bash
ss -lptn 'sport = :5173'    # also $RINGRPQ_BACKEND_PORT, $RINGRPQ_XPATH_PORT, 5432
kill <pid>
```
(`lsof` is not installed by default on Ubuntu; `ss` comes with `iproute2`.)

---

## Server (systemd)

### Installing the units
```bash
cd $REPO
sudo -E ./deploy/install-units.sh     # fills in the templates, installs, enables
sudo ./deploy/enable-workers.sh       # one worker unit per discovered database
sudo systemctl start ringrpq.target
```

The units in `deploy/systemd/` are templated (`@REPO@`, `@USER@`, `@UVICORN@`, …) rather than
committed ready-to-use, so the same files work on a dev box and a server.
`install-units.sh` fills them in and accepts overrides:

| Variable | Default |
|---|---|
| `RINGRPQ_USER` | the invoking user |
| `UVICORN` | the `ringrpq-backend` conda env's `uvicorn` |
| `JAVA` | `command -v java` |
| `BACKEND_HOST` | `127.0.0.1` |
| `CONNINFO` | read from `backend/.env` |
| `RINGRPQ_BACKEND_PORT` | read from `ports.conf` (8000) |
| `RINGRPQ_XPATH_PORT` | read from `ports.conf` (8080) |

The two ports are baked into the units, so re-run this script after editing `ports.conf`
(and note `sudo -E`, without which an overriding `RINGRPQ_*_PORT` in your environment is
dropped by sudo).

`BACKEND_HOST` defaults to loopback. Bind publicly only behind a reverse proxy terminating
TLS, or pass `BACKEND_HOST=0.0.0.0` for a demo box on a trusted network.

Because conda is used for the backend, the unit invokes the env's `uvicorn` by **absolute
path** — there's no `conda activate`, which needs an interactive shell systemd doesn't provide.

### Operating
```bash
systemctl status 'ringrpq*'
sudo systemctl restart ringrpq-backend
sudo systemctl stop ringrpq.target       # stops all of it (units are PartOf= the target)
journalctl -u ringrpq-backend -f
journalctl -u 'ringrpq-worker@*' --since "10 min ago"
```

Worker units are templated on the database name. Those names contain `/`, which systemd
instance names can't hold verbatim, so they must be addressed through `systemd-escape` —
`deploy/enable-workers.sh` does it for you:

```bash
sudo ./deploy/enable-workers.sh           # enable + start any missing workers (idempotent)
sudo ./deploy/enable-workers.sh status    # per-database unit state
sudo ./deploy/enable-workers.sh disable   # stop + disable every instance
```

By hand, for one database:
```bash
sudo systemctl restart "ringrpq-worker@$(systemd-escape 'test_correctness/test.dat').service"
```

Adding a database later: drop its files under `Ring-RPQ/data/`, build its index, then re-run
`sudo ./deploy/enable-workers.sh`. Only the new database gets a unit; existing workers are not
restarted.

### After rebuilding
Workers load their index once at startup, so a new binary or a re-generated index needs a
restart:
```bash
sudo systemctl restart 'ringrpq-worker@*'
```

---

## Database folder location

Defaults to `Ring-RPQ/data/`, but doesn't have to live there. Set it **once**:

```bash
echo "/path/to/external/data" > $REPO/Ring-RPQ/data-dir.conf
```

`data-dir.conf` (gitignored, single line, `#` comments allowed) is read by
`Ring-RPQ/worker-discovery.sh` (and therefore `run-workers.sh` and
`deploy/enable-workers.sh`), by `ring-rpq-worker` itself, and by the backend's
`GET /query/databases` — so this one file is the single source of truth.

Per-component overrides: `$RING_RPQ_DATA_DIR` beats the file for the scripts and the worker;
`RING_RPQ_DATA_DIR` in `backend/.env` beats it for the backend. **systemd units don't inherit
a login shell**, so an env-var override there has to go in the unit as
`Environment=RING_RPQ_DATA_DIR=…` — the `data-dir.conf` file needs no such handling, since the
worker's `WorkingDirectory` is `Ring-RPQ/`.

`ring-rpq-worker` resolves both the data dir and the `run/` pid file it writes **relative to
its working directory**, so a manually started worker must be run with cwd = `Ring-RPQ/`.

---

## How a query actually flows

Worth knowing when something looks stuck:

1. `POST /query/submit` → backend calls XPathRingRPQ `/translate`, then inserts a `pending`
   row in `query_cache` and returns its id. A translation failure never reaches the queue; it
   is recorded in `translation_failures` instead.
2. The worker for that `database_name` claims the oldest `pending` row
   (`FOR UPDATE SKIP LOCKED`), after first sweeping expired `pending` rows for *all*
   databases.
3. It checks `query_cache` for a reusable `completed` result — same `database_name` +
   `translated_query`, and either complete or already holding at least as many rows as
   requested. A hit is trimmed in SQL and written straight back.
4. Otherwise it calls `evaluate()` against the in-memory index.
5. `None` → `completed`; `ReachedBound` → `completed` with `truncated: true`; `TimedOut` →
   `failed` / `'Timeout reached'`; parse errors → `failed` with a message.
6. The frontend polls `GET /query/rpq/{id}` with backoff until the status is terminal.

A worker is advertised as available via its pid file in `Ring-RPQ/run/` — the worker writes it
itself once its index is loaded *and* its database connection is up, and removes it on clean
shutdown. `GET /query/databases` only lists a graph whose worker is live by that test, so a
database with a dead worker correctly disappears from the UI dropdown.

---

## Quick health check
```bash
source $REPO/ports.sh
pg_isready -h localhost -p 5432 && echo "Postgres OK"
curl -sf "http://localhost:$RINGRPQ_XPATH_PORT/translate?query=%3Fx%20%3Chas%3E%20%3Fy" >/dev/null && echo "XPathRingRPQ OK"
curl -sf http://localhost:$RINGRPQ_BACKEND_PORT/health && echo " Backend OK"
curl -s http://localhost:$RINGRPQ_BACKEND_PORT/query/databases   # non-empty => a worker is live
$REPO/Ring-RPQ/run-workers.sh status                     # dev
sudo $REPO/deploy/enable-workers.sh status               # server
curl -sf http://localhost:5173 >/dev/null && echo "Frontend OK"
```

End-to-end, which is the only check that exercises translate → queue → worker → poll:
```bash
ID=$(curl -s -X POST http://localhost:$RINGRPQ_BACKEND_PORT/query/submit -H 'Content-Type: application/json' \
  -d '{"query":"*/a/*","database_name":"test_correctness/test.dat","max_number_of_results":10,"time_out_limit":30}' \
  | python3 -c "import sys,json;print(json.load(sys.stdin)['query_cache_id'])")
sleep 3 && curl -s http://localhost:$RINGRPQ_BACKEND_PORT/query/rpq/$ID
```

---

## WSL notes

- systemd is required for the units above. Check `/etc/wsl.conf` contains:
  ```ini
  [boot]
  systemd=true
  ```
  then `wsl --shutdown` from Windows to apply.
- `localhost` forwards both directions, so a browser on Windows reaches
  http://localhost:5173 without extra configuration.
- Keep the checkout on the **Linux** filesystem (`~/Projects/…`), not `/mnt/c/…`. Builds and
  `npm install` are dramatically slower across the Windows mount.
- Verify `command -v node` is under `/usr/` and not `/mnt/c/...` before running `npm install`.
  See `SETUP.md`.
