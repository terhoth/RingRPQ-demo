# worker-discovery.sh — sourceable helpers shared by run-workers.sh (the dev
# launcher) and deploy/enable-workers.sh (the systemd one), so the rule for
# "which databases are usable" lives in exactly one place per launcher path.
#
# REQUIRED_SUFFIXES below must stay in sync with REQUIRED_GRAPH_SUFFIXES in
# backend/app/routers/query.py — that file duplicates the rule rather than
# calling into this one, so a database is only offered in the UI when a
# worker could also have been started for it.
#
# Source it, don't execute it:
#     source "$(dirname "${BASH_SOURCE[0]}")/worker-discovery.sh"
#
# Sets:     RING_RPQ_HOME, DATA_DIR, and exports RING_RPQ_DATA_DIR if resolved
# Defines:  discover_databases, sanitize
#
# The database folder location is resolved in order of precedence:
#   1. $RING_RPQ_DATA_DIR, if already set
#   2. data-dir.conf in Ring-RPQ/ (gitignored, single line, '#' comments
#      allowed) — the same file ring-rpq-worker and the backend read, so the
#      location only needs setting once
#   3. Ring-RPQ/data/
# Prefer an absolute path in data-dir.conf — it is not resolved relative to
# anything.

RING_RPQ_HOME="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DATA_DIR_CONFIG_FILE="$RING_RPQ_HOME/data-dir.conf"
if [[ -z "${RING_RPQ_DATA_DIR:-}" && -f "$DATA_DIR_CONFIG_FILE" ]]; then
    RING_RPQ_DATA_DIR="$(grep -vE '^[[:space:]]*(#|$)' "$DATA_DIR_CONFIG_FILE" | head -n1 | sed -E 's/^[[:space:]]+|[[:space:]]+$//g')"
fi
# Export only if non-empty, so worker processes inherit the resolved value —
# an exported-but-empty var would shadow ring-rpq-worker's own fallback.
[[ -n "${RING_RPQ_DATA_DIR:-}" ]] && export RING_RPQ_DATA_DIR

DATA_DIR="${RING_RPQ_DATA_DIR:-$RING_RPQ_HOME/data}"

# Sibling files a base graph file (x.dat) must have to be a usable index.
REQUIRED_SUFFIXES=(.SO .P .A .nTriples .attr.str .attr.int .attr.flt .L_S .C_S .bwtp.L .bwtp.C)

# Prints one database_name per line (relative to the data dir, e.g.
# "foo/foo.dat"), sorted, for every *.dat that has all its required siblings.
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
# Mirrors ring-rpq-worker's pid_file_path() and query.py's replace('/', '_').
sanitize() { printf '%s' "$1" | tr '/' '_'; }
