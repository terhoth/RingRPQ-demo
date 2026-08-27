from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str
    xpathringrpq_url: str
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
