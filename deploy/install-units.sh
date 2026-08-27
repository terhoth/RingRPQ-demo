#!/usr/bin/env bash
#
# install-units.sh — fill in the @PLACEHOLDER@s in deploy/systemd/*.service
# and install the result into /etc/systemd/system.
#
# The units are templated rather than committed ready-to-use so the same
# files work in a WSL dev box and on a server, where the repo path, the user,
# and the conda prefix all differ.
#
# Usage (needs sudo — it writes to /etc/systemd/system):
#   sudo -E ./deploy/install-units.sh
#
# Overridable via the environment (defaults in parentheses):
#   RINGRPQ_USER    account the services run as    (the invoking user)
#   UVICORN         path to the conda env's uvicorn
#                   ($CONDA_PREFIX/../ringrpq-backend/bin/uvicorn, else `command -v`)
#   JAVA            path to the JVM                (`command -v java`)
#   BACKEND_HOST    uvicorn bind address           (127.0.0.1)
#   CONNINFO        libpq URI for the workers      (read from backend/.env)
#
# BACKEND_HOST defaults to loopback: bind the backend publicly only behind a
# reverse proxy that terminates TLS. Set BACKEND_HOST=0.0.0.0 to expose it
# directly (a demo box on a trusted network).
#
# After this, enable one worker per database with deploy/enable-workers.sh.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_SRC="$REPO/deploy/systemd"
UNIT_DEST="/etc/systemd/system"

# SUDO_USER is the human who ran sudo; $USER would be root.
RINGRPQ_USER="${RINGRPQ_USER:-${SUDO_USER:-$USER}}"
USER_HOME="$(getent passwd "$RINGRPQ_USER" | cut -d: -f6)"

if [[ -z "${UVICORN:-}" ]]; then
    for candidate in \
        "$USER_HOME/miniconda3/envs/ringrpq-backend/bin/uvicorn" \
        "$USER_HOME/anaconda3/envs/ringrpq-backend/bin/uvicorn"; do
        [[ -x "$candidate" ]] && UVICORN="$candidate" && break
    done
fi
UVICORN="${UVICORN:-$(command -v uvicorn || true)}"
JAVA="${JAVA:-$(command -v java || true)}"
BACKEND_HOST="${BACKEND_HOST:-127.0.0.1}"

# The workers speak libpq directly, so they need the plain URI form, not
# SQLAlchemy's "postgresql+psycopg2://".
if [[ -z "${CONNINFO:-}" ]]; then
    CONNINFO="$(grep -E '^DATABASE_URL=' "$REPO/backend/.env" 2>/dev/null | head -n1 | cut -d= -f2- | sed 's/+psycopg2//')"
fi
CONNINFO="${CONNINFO:-postgresql://ringrpq:ringrpq@localhost:5432/ringrpq}"

for var in UVICORN JAVA; do
    if [[ -z "${!var}" ]]; then
        echo "install-units.sh: could not determine $var — set it explicitly, e.g. sudo -E $var=/path/to/bin $0" >&2
        exit 1
    fi
done

echo "install-units.sh: installing units"
echo "  repo:         $REPO"
echo "  user:         $RINGRPQ_USER"
echo "  uvicorn:      $UVICORN"
echo "  java:         $JAVA"
echo "  backend host: $BACKEND_HOST"
echo "  conninfo:     ${CONNINFO%%:*}://…"

for unit in "$UNIT_SRC"/*.service "$UNIT_SRC"/*.target; do
    name="$(basename "$unit")"
    sed -e "s|@REPO@|$REPO|g" \
        -e "s|@USER@|$RINGRPQ_USER|g" \
        -e "s|@UVICORN@|$UVICORN|g" \
        -e "s|@JAVA@|$JAVA|g" \
        -e "s|@BACKEND_HOST@|$BACKEND_HOST|g" \
        -e "s|@CONNINFO@|$CONNINFO|g" \
        "$unit" >"$UNIT_DEST/$name"
    echo "  -> $UNIT_DEST/$name"
done

systemctl daemon-reload
systemctl enable ringrpq.target ringrpq-backend.service ringrpq-xpath.service

cat <<MSG

Installed. Next:
  sudo $REPO/deploy/enable-workers.sh   # one worker unit per database
  sudo systemctl start ringrpq.target
  systemctl status 'ringrpq*'
MSG
