#!/usr/bin/env bash
#
# enable-workers.sh — enable and start one ringrpq-worker@ instance per
# usable database, the systemd counterpart to Ring-RPQ/run-workers.sh.
#
# Uses the same discovery rule as run-workers.sh and the backend (see
# Ring-RPQ/worker-discovery.sh), so a database that shows up in the UI is one
# that got a worker.
#
# Idempotent, and re-running it is the rescan: drop a new database's files
# under Ring-RPQ/data/, run this again, and only the new database gets a
# unit — existing workers are not restarted.
#
# Usage (needs sudo):
#   sudo ./deploy/enable-workers.sh           # enable + start missing workers
#   sudo ./deploy/enable-workers.sh status    # per-database unit state
#   sudo ./deploy/enable-workers.sh disable   # stop + disable every instance

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO/Ring-RPQ/worker-discovery.sh"

# database_names contain '/', which systemd instance names can't hold
# verbatim; systemd-escape encodes them, and the unit's %I decodes them back.
unit_for() { printf 'ringrpq-worker@%s.service' "$(systemd-escape "$1")"; }

databases="$(discover_databases)"
if [[ -z "$databases" ]]; then
    echo "enable-workers.sh: no usable databases found under $DATA_DIR" >&2
    echo "  (build an index first: Ring-RPQ/build/build-index-attr <path>.dat)" >&2
    exit 0
fi

case "${1:-enable}" in
enable)
    while IFS= read -r db; do
        [[ -z "$db" ]] && continue
        unit="$(unit_for "$db")"
        if systemctl is-active --quiet "$unit"; then
            echo "  already running  $db"
            continue
        fi
        systemctl enable --now "$unit" && echo "  started          $db  ($unit)"
    done <<<"$databases"
    ;;
status)
    while IFS= read -r db; do
        [[ -z "$db" ]] && continue
        printf '  %-9s %s\n' "$(systemctl is-active "$(unit_for "$db")")" "$db"
    done <<<"$databases"
    ;;
disable)
    while IFS= read -r db; do
        [[ -z "$db" ]] && continue
        unit="$(unit_for "$db")"
        systemctl disable --now "$unit" 2>/dev/null && echo "  stopped          $db"
    done <<<"$databases"
    ;;
*)
    echo "Usage: $0 [enable|status|disable]" >&2
    exit 1
    ;;
esac
