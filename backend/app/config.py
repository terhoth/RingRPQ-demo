import os
from pathlib import Path

from pydantic import Field
from pydantic_settings import BaseSettings

REPO_ROOT = Path(__file__).resolve().parents[2]
# KEY=value lines, '#' comments allowed — the same file ports.sh (and through
# it deploy/install-units.sh), XPathRingRPQ and frontend/vite.config.js read,
# so a port only needs setting in one place. Parsed here rather than shelling
# out to ports.sh, the way routers/query.py parses Ring-RPQ/data-dir.conf.
PORTS_CONFIG_FILE = REPO_ROOT / "ports.conf"


def read_port(key: str, default: int) -> int:
    """$KEY, then ports.conf, then default — ports.sh's precedence exactly."""
    raw = os.environ.get(key)
    if raw is None and PORTS_CONFIG_FILE.is_file():
        for line in PORTS_CONFIG_FILE.read_text().splitlines():
            name, sep, value = line.split("#", 1)[0].partition("=")
            if sep and name.strip() == key:
                raw = value.strip()  # last assignment wins, as in ports.sh
    if raw is None or not raw.strip():
        return default
    try:
        port = int(raw)
    except ValueError:
        port = -1
    if not 1 <= port <= 65535:
        raise ValueError(
            f"{key} is {raw!r}, which is not a TCP port number (see {PORTS_CONFIG_FILE})"
        )
    return port


class Settings(BaseSettings):
    database_url: str
    # Where the XPathRingRPQ translation service listens. Normally left unset
    # and derived from RINGRPQ_XPATH_PORT / ports.conf; set it explicitly only
    # when that service runs somewhere other than this host.
    xpathringrpq_url: str = Field(
        default_factory=lambda: f"http://localhost:{read_port('RINGRPQ_XPATH_PORT', 8080)}"
    )
    frontend_origin: str
    # Dev convenience: also accept any http://localhost:<port> origin, since
    # Vite walks to 5174, 5175, ... when 5173 is taken. Must stay off in
    # production, where the browser origin is the server's own.
    allow_localhost_origins: bool = False
    # Overrides the Ring-RPQ database folder (default: Ring-RPQ/data/, next
    # to this repo checkout). Usually left unset — prefer setting the path
    # once in Ring-RPQ/data-dir.conf, which routers/query.py also falls back
    # to; this takes precedence over that file when set.
    ring_rpq_data_dir: str | None = None

    class Config:
        env_file = ".env"


settings = Settings()
