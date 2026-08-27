# ports.sh — sourceable resolver for the service ports in ports.conf, so
# every shell-side launcher reads that file the same way.
#
# Source it, don't execute it:
#     source "$(dirname "${BASH_SOURCE[0]}")/ports.sh"
#
# Exports: RINGRPQ_BACKEND_PORT, RINGRPQ_XPATH_PORT
#
# Precedence per variable: an already-set environment variable wins, then
# ports.conf next to this file, then the built-in default — the same shape as
# Ring-RPQ/worker-discovery.sh's data-dir resolution. Exporting means a
# service started from this shell (uvicorn, ./mvnw spring-boot:run) inherits
# the resolved value.
#
# ports.conf is parsed rather than sourced: deploy/install-units.sh runs
# under sudo, and sourcing would hand root whatever is in a file the repo
# owner can edit.
#
# Safe under `set -e` — every conditional is a full if/fi, not a && chain.

RINGRPQ_HOME="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORTS_CONFIG_FILE="$RINGRPQ_HOME/ports.conf"

# Prints the value assigned to $1 in ports.conf, or nothing. Last assignment
# wins, '#' comments and surrounding whitespace are stripped.
_ports_conf_value() {
    if [[ ! -f "$PORTS_CONFIG_FILE" ]]; then
        return 0
    fi
    grep -E "^[[:space:]]*$1[[:space:]]*=" "$PORTS_CONFIG_FILE" \
        | tail -n1 \
        | cut -d= -f2- \
        | sed -E 's/#.*$//; s/^[[:space:]]+//; s/[[:space:]]+$//'
}

# _resolve_port VAR_NAME DEFAULT — sets and exports VAR_NAME.
_resolve_port() {
    local name="$1" default="$2" value
    value="${!name:-}"
    if [[ -z "$value" ]]; then
        value="$(_ports_conf_value "$name")"
    fi
    if [[ -z "$value" ]]; then
        value="$default"
    fi
    # Catches a typo'd ports.conf here, where the message names the variable,
    # rather than as a confusing bind failure inside uvicorn or the JVM.
    if [[ ! "$value" =~ ^[0-9]+$ ]] || (( value < 1 || value > 65535 )); then
        echo "ports.sh: $name is '$value', which is not a TCP port number (see $PORTS_CONFIG_FILE)" >&2
        return 1
    fi
    printf -v "$name" '%s' "$value"
    export "$name"
}

_resolve_port RINGRPQ_BACKEND_PORT 8000
_resolve_port RINGRPQ_XPATH_PORT 8080
