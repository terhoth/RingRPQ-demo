import json
import os
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

# run-workers.sh tracks each database's worker via a pid file here, named by
# sanitize() (database_name with '/' -> '_'). Mirrored here rather than
# calling the script so a database is only reported as usable once its
# worker can actually drain query_cache rows for it.
RING_RPQ_RUN_DIR = RING_RPQ_DIR / "run"


def _worker_is_running(database_name: str) -> bool:
    pid_file = RING_RPQ_RUN_DIR / f"{database_name.replace('/', '_')}.pid"
    if not pid_file.is_file():
        return False
    try:
        pid = int(pid_file.read_text().strip())
        os.kill(pid, 0)
    except (OSError, ValueError):
        return False
    return True

# Optional file at the data dir's root mapping a graph's relative .dat path (the
# same string list_databases() returns) to that graph's documentation entry:
#
#     {"some/graph.dat": {"links": ["https://…", "https://…"], "note": "…",
#                         "prefixes": {"ex": "http://example.org/"}}}
#
# "links" holds any number of documentation URLs and "note" a literal
# instruction the frontend shows verbatim below them; either may be omitted, and
# a bare string value is still accepted as shorthand for a single link. Entries
# for graphs that aren't currently available are simply ignored by the frontend,
# so this can be a superset of what list_databases() finds.
#
# "prefixes" is what makes namespace-prefixed names (yago:Belgium) usable on a
# graph whose dictionary holds full URIs: submit_query hands the map to
# XPathRingRPQ, which expands "yago:Belgium" to the full URI while translating.
# It is per-graph and entirely opt-in — a graph without a "prefixes" entry (all
# of them but yago.dat today) is translated exactly as before, with prefixed
# names still rejected. tools/graph-prefixes.py derives the map for a graph from
# its source Turtle header and built dictionaries.
GRAPH_DOC_FILE = RING_RPQ_DATA_DIR / "graph_doc.json"


def _parse_graph_doc(value: object) -> dict[str, object] | None:
    if isinstance(value, str):
        return {"links": [value], "note": "", "prefixes": {}}
    if not isinstance(value, dict):
        return None

    raw_links = value.get("links")
    if isinstance(raw_links, str):
        links = [raw_links]
    elif isinstance(raw_links, list):
        links = [link for link in raw_links if isinstance(link, str)]
    else:
        links = []

    note = value.get("note")
    if not isinstance(note, str):
        note = ""

    raw_prefixes = value.get("prefixes")
    if isinstance(raw_prefixes, dict):
        # Both halves must be strings, and a namespace is only useful if there
        # is a prefix to reach it by — an empty prefix would mean "expand every
        # bare name", which is deliberately not supported.
        prefixes = {
            prefix: namespace
            for prefix, namespace in raw_prefixes.items()
            if isinstance(prefix, str) and prefix and isinstance(namespace, str) and namespace
        }
    else:
        prefixes = {}

    if not links and not note and not prefixes:
        return None
    return {"links": links, "note": note, "prefixes": prefixes}


def _prefixes_for(database_name: str) -> dict[str, str]:
    """The namespace prefixes declared for one graph in graph_doc.json, or {} if
    it declares none — in which case the query is translated with no expansion
    step at all, exactly as before this was added."""
    entry = _read_graph_docs().get(database_name) or {}
    prefixes = entry.get("prefixes")
    return prefixes if isinstance(prefixes, dict) else {}


def _read_graph_docs() -> dict[str, dict[str, object]]:
    if not GRAPH_DOC_FILE.is_file():
        return {}
    try:
        data = json.loads(GRAPH_DOC_FILE.read_text())
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(data, dict):
        return {}

    docs = {}
    for key, value in data.items():
        if not isinstance(key, str):
            continue
        entry = _parse_graph_doc(value)
        if entry is not None:
            docs[key] = entry
    return docs


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


# Prefixes used by XPathRingRPQ's /translate endpoint (see XPathErrorListener and
# XPathRpqListenerImpl in the Java project) to tag entries in its "errors" list.
SYNTAX_ERROR_PREFIX = "Syntax error: "
SYNTAX_WARNING_PREFIX = "Syntax warning: "


def _first_message(errors: list[str], prefix: str) -> str | None:
    for entry in errors:
        if entry.startswith(prefix):
            return entry[len(prefix) :]
    return None


@router.get("/databases")
def list_databases():
    graphs = []
    for dat_file in sorted(RING_RPQ_DATA_DIR.rglob("*.dat")):
        if not all(Path(f"{dat_file}{suffix}").is_file() for suffix in REQUIRED_GRAPH_SUFFIXES):
            continue
        database_name = dat_file.relative_to(RING_RPQ_DATA_DIR).as_posix()
        if _worker_is_running(database_name):
            graphs.append(database_name)
    return {"graphs": graphs, "docs": _read_graph_docs()}


@router.post("/submit")
def submit_query(request: QueryRequest, db: Annotated[Session, Depends(get_db)]):
    params = {"query": request.query}
    # Only graphs that declare prefixes get the expansion step; for every other
    # graph the request is byte-for-byte what it was before, and XPathRingRPQ
    # falls back to its unprefixed behaviour.
    prefixes = _prefixes_for(request.database_name)
    if prefixes:
        params["prefixes"] = json.dumps(prefixes)

    with httpx.Client() as client:
        resp = client.get(f"{settings.xpathringrpq_url}/translate", params=params)

    data = resp.json() if resp.is_success else {}
    translated_query = data.get("query")
    errors = data.get("errors") or []

    syntax_error_message = _first_message(errors, SYNTAX_ERROR_PREFIX)
    syntax_warning_message = _first_message(errors, SYNTAX_WARNING_PREFIX)

    if translated_query and not syntax_error_message:
        entry = QueryCache(
            user_query=request.query,
            database_name=request.database_name,
            max_number_of_results=request.max_number_of_results,
            time_out_limit=request.time_out_limit,
            translated_query=translated_query,
            translation_warning_message=syntax_warning_message,
        )
    else:
        entry = TranslationFailure(
            user_query=request.query,
            database_name=request.database_name,
            max_number_of_results=request.max_number_of_results,
            time_out_limit=request.time_out_limit,
            error_message=syntax_error_message,
        )

    db.add(entry)
    db.commit()

    if isinstance(entry, TranslationFailure):
        return {
            "status": "translation_failed",
            "translation_failure_id": entry.id,
            "error_message": entry.error_message,
        }
    return {"status": "translated", "query_cache_id": entry.id}


@router.get("/rpq/{id}")
def get_rpq_result(id: int, db: Annotated[Session, Depends(get_db)]):
    entry = db.get(QueryCache, id)
    if entry is None:
        raise HTTPException(status_code=404, detail="Query not found")

    result = entry.result
    if isinstance(result, list):
        result = sorted(result, key=lambda row: (row.get("subject"), row.get("object")))

    return {
        "id": entry.id,
        "status": entry.status,
        "result": result,
        "error_message": entry.error_message,
        "translation_warning_message": entry.translation_warning_message,
        "submitted_at": entry.submitted_at,
        "completed_at": entry.completed_at,
    }

