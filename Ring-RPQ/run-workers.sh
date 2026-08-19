#!/usr/bin/env bash
#
# run-workers.sh — start one ring-rpq-worker instance per usable database.
#
# Discovers databases under data/ the same way backend/app/routers/query.py's
# list_databases() does (REQUIRED_SUFFIXES below must stay in sync with that
# file's REQUIRED_GRAPH_SUFFIXES if it ever changes — this script duplicates
# that rule rather than calling the backend, so it works standalone without
# the backend needing to be up).
#
# Idempotent: re-running this (e.g. after dropping a new database's files
# into data/) only starts workers for databases that don't already have a
# live one running, tracked via pid files in run/. Already-running workers —
# and whatever row they're mid-query on — are left completely alone. That's
# also how you rescan for newly added databases: just run this again.
#
# Usage:
#   ./run-workers.sh [start]   # default — discover + launch missing workers
#   ./run-workers.sh status    # list discovered databases and worker state
#   ./run-workers.sh stop      # stop every worker this script started
#
# Must be runnable from anywhere; it locates itself and works from Ring-RPQ/
# regardless of the caller's cwd.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

DATA_DIR="$SCRIPT_DIR/data"
RUN_DIR="$SCRIPT_DIR/run"
WORKER_BIN="$SCRIPT_DIR/build/ring-rpq-worker"

# Sibling files a base graph file (x.dat) must have to be a usable index —
# mirrors backend/app/routers/query.py's REQUIRED_GRAPH_SUFFIXES.
REQUIRED_SUFFIXES=(.SO .P .A .nTriples .attr.str .attr.int .attr.flt .L_S .C_S .bwtp.L .bwtp.C)

# Prints one database_name per line (relative to data/, e.g. "foo/foo.dat"),
# sorted, for every *.dat under data/ that has all its required siblings.
discover_databases() {
    [[ -d "$DATA_DIR" ]] || return 0
    find "$DATA_DIR" -name "*.dat" | sort | while IFS= read -r dat_file; do
        local ok=1
        for suf in "${REQUIRED_SUFFIXES[@]}"; do
            if [[ ! -f "${dat_file}${suf}" ]]; then
                ok=0
                break
            fi
        done
        if [[ $ok -eq 1 ]]; then
            echo "${dat_file#"$DATA_DIR"/}"
        fi
    done
}

# Filesystem-safe token for pid/log filenames — database_names contain '/'.
sanitize() { printf '%s' "$1" | tr '/' '_'; }

pid_file_for() { printf '%s/%s.pid' "$RUN_DIR" "$(sanitize "$1")"; }
log_file_for() { printf '%s/%s.log' "$RUN_DIR" "$(sanitize "$1")"; }

# True iff a worker this script started for database_name "$1" is still alive.
is_running() {
    local pid_file
    pid_file="$(pid_file_for "$1")"
    [[ -f "$pid_file" ]] || return 1
    local pid
    pid="$(cat "$pid_file")"
    [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

cmd_start() {
    if [[ ! -x "$WORKER_BIN" ]]; then
        echo "run-workers.sh: $WORKER_BIN not found — build it first" >&2
        echo "  (cd $SCRIPT_DIR/build && cmake .. && make ring-rpq-worker)" >&2
        exit 1
    fi
    mkdir -p "$RUN_DIR"

    local databases
    databases="$(discover_databases)"
    if [[ -z "$databases" ]]; then
        echo "run-workers.sh: no usable databases found under $DATA_DIR"
        return 0
    fi

    local started=0 skipped=0
    while IFS= read -r db; do
        [[ -z "$db" ]] && continue
        if is_running "$db"; then
            echo "run-workers.sh: $db already running (pid $(cat "$(pid_file_for "$db")"))"
            skipped=$((skipped + 1))
            continue
        fi
        local log_file pid_file
        log_file="$(log_file_for "$db")"
        pid_file="$(pid_file_for "$db")"
        nohup "$WORKER_BIN" "$db" >"$log_file" 2>&1 &
        echo $! >"$pid_file"
        echo "run-workers.sh: started $db (pid $!, log $log_file)"
        started=$((started + 1))
    done <<<"$databases"

    echo "run-workers.sh: $started started, $skipped already running"
}

cmd_status() {
    local databases
    databases="$(discover_databases)"
    if [[ -z "$databases" ]]; then
        echo "run-workers.sh: no usable databases found under $DATA_DIR"
        return 0
    fi
    while IFS= read -r db; do
        [[ -z "$db" ]] && continue
        if is_running "$db"; then
            echo "  RUNNING  $db  (pid $(cat "$(pid_file_for "$db")"))"
        else
            echo "  STOPPED  $db"
        fi
    done <<<"$databases"
}

cmd_stop() {
    if [[ ! -d "$RUN_DIR" ]]; then
        echo "run-workers.sh: nothing to stop ($RUN_DIR doesn't exist)"
        return 0
    fi
    local stopped=0
    for pid_file in "$RUN_DIR"/*.pid; do
        [[ -e "$pid_file" ]] || continue
        local pid db
        pid="$(cat "$pid_file")"
        db="$(basename "$pid_file" .pid)"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid"
            echo "run-workers.sh: stopped $db (pid $pid)"
            stopped=$((stopped + 1))
        fi
        rm -f "$pid_file"
    done
    echo "run-workers.sh: $stopped worker(s) stopped"
}

case "${1:-start}" in
start) cmd_start ;;
status) cmd_status ;;
stop) cmd_stop ;;
*)
    echo "Usage: $0 [start|status|stop]" >&2
    exit 1
    ;;
esac
