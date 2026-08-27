from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from .config import settings
from .routers import health, query

app = FastAPI(title="RingRPQ API")

# Vite falls back to 5174, 5175, ... when 5173 is already taken (e.g. a leftover
# `npm run dev` from an earlier session), so in development any localhost port is
# accepted. Off by default — in production the frontend is served from this same
# origin (see the static mount below) and no wildcard is warranted.
app.add_middleware(
    CORSMiddleware,
    allow_origins=[settings.frontend_origin],
    allow_origin_regex=r"http://localhost:\d+" if settings.allow_localhost_origins else None,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(health.router)
app.include_router(query.router)

# frontend/build_and_copy.py builds the frontend and copies the output here.
# Mounted last so it only catches requests the API routers above don't handle.
STATIC_DIR = Path(__file__).resolve().parents[2] / "static"
if STATIC_DIR.is_dir():
    app.mount("/", StaticFiles(directory=STATIC_DIR, html=True), name="frontend")
