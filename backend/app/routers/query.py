import httpx
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from ..config import settings

router = APIRouter(prefix="/query", tags=["query"])


class QueryRequest(BaseModel):
    query: str
    database_name: str
    max_number_of_results: int
    time_out_limit: int


@router.get("/databases")
def list_databases():
    # Should return the list of available database names to populate the frontend selector.
    raise HTTPException(status_code=501, detail="Database listing not implemented yet")


@router.post("/submit")
def submit_query(request: QueryRequest):
    # Should run `request.query` against `request.database_name`, bounded by
    # `request.max_number_of_results` and `request.time_out_limit`.
    raise HTTPException(status_code=501, detail="Query submission not implemented yet")


@router.get("/xpath/translate")
async def translate_xpath(query: str):
    async with httpx.AsyncClient() as client:
        resp = await client.get(f"{settings.xpathringrpq_url}/translate", params={"query": query})
    resp.raise_for_status()
    return resp.json()


@router.get("/xpath/execute")
async def execute_xpath(query: str):
    async with httpx.AsyncClient() as client:
        resp = await client.get(f"{settings.xpathringrpq_url}/execute", params={"query": query})
    resp.raise_for_status()
    return resp.json()


@router.get("/rpq")
def execute_rpq(query: str):
    # Ring-RPQ is invoked as a subprocess against on-disk indexes (see SETUP.md Step 5).
    # Not wired up yet.
    raise HTTPException(status_code=501, detail="Ring-RPQ integration not implemented yet (see SETUP.md Step 5)")
