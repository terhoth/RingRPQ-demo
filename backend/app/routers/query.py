from pathlib import Path
from typing import Annotated

import httpx
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from sqlalchemy.orm import Session

from ..config import settings
from ..db import get_db
from ..models import QueryCache, TranslationFailure

router = APIRouter(prefix="/query", tags=["query"])

RING_RPQ_DIR = Path(__file__).resolve().parents[3] / "Ring-RPQ"
# Single-line path, '#' comments allowed — the same file run-workers.sh and
# ring-rpq-worker read, so the database folder location only needs setting
# once (gitignored; see Ring-RPQ/run-workers.sh's header comment).
DATA_DIR_CONFIG_FILE = RING_RPQ_DIR / "data-dir.conf"


def _read_data_dir_config_file() -> str | None:
    if not DATA_DIR_CONFIG_FILE.is_file():
        return None
    for line in DATA_DIR_CONFIG_FILE.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    return None


def _resolve_data_dir() -> Path:
    # Order of precedence: $RING_RPQ_DATA_DIR / backend/.env, then
    # data-dir.conf, then the Ring-RPQ/data/ default.
    override = settings.ring_rpq_data_dir or _read_data_dir_config_file()
    if override:
        return Path(override).expanduser().resolve()
    return RING_RPQ_DIR / "data"


RING_RPQ_DATA_DIR = _resolve_data_dir()

# Sibling files a base graph file (x.dat) must have alongside it to be a usable index.
REQUIRED_GRAPH_SUFFIXES = [
    # source graph files
    ".SO",
    ".P",
    ".A",
    # generated index files
    ".nTriples",
    ".attr.str",
    ".attr.int",
    ".attr.flt",
    ".L_S",
    ".C_S",
    ".bwtp.L",
    ".bwtp.C",
]


class QueryRequest(BaseModel):
    query: str
    database_name: str
    max_number_of_results: int
    time_out_limit: int


@router.get("/databases")
def list_databases():
    graphs = []
    for dat_file in sorted(RING_RPQ_DATA_DIR.rglob("*.dat")):
        if all(Path(f"{dat_file}{suffix}").is_file() for suffix in REQUIRED_GRAPH_SUFFIXES):
            graphs.append(dat_file.relative_to(RING_RPQ_DATA_DIR).as_posix())
    return {"graphs": graphs}


@router.post("/submit")
def submit_query(request: QueryRequest, db: Annotated[Session, Depends(get_db)]):
    with httpx.Client() as client:
        resp = client.get(f"{settings.xpathringrpq_url}/translate", params={"query": request.query})

    translated_query = resp.json().get("query") if resp.is_success else None

    if translated_query:
        entry = QueryCache(
            user_query=request.query,
            database_name=request.database_name,
            max_number_of_results=request.max_number_of_results,
            time_out_limit=request.time_out_limit,
            translated_query=translated_query,
        )
    else:
        entry = TranslationFailure(
            user_query=request.query,
            database_name=request.database_name,
            max_number_of_results=request.max_number_of_results,
            time_out_limit=request.time_out_limit,
        )

    db.add(entry)
    db.commit()

    if isinstance(entry, TranslationFailure):
        return {"status": "translation_failed", "translation_failure_id": entry.id}
    return {"status": "translated", "query_cache_id": entry.id}


@router.get("/rpq/{id}")
def get_rpq_result(id: int, db: Annotated[Session, Depends(get_db)]):
    entry = db.get(QueryCache, id)
    if entry is None:
        raise HTTPException(status_code=404, detail="Query not found")
    return {
        "id": entry.id,
        "status": entry.status,
        "result": entry.result,
        "error_message": entry.error_message,
        "submitted_at": entry.submitted_at,
        "completed_at": entry.completed_at,
    }

