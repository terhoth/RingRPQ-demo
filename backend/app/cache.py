import hashlib

from sqlalchemy import select
from sqlalchemy.orm import Session

from .models import QueryCache


def _hash(translated_query: str) -> bytes:
    """Must match the DB-side `sha256(translated_query::bytea)` generated column
    (server_encoding is UTF8, so this is exactly the same bytes Postgres hashes).
    """
    return hashlib.sha256(translated_query.encode("utf-8")).digest()


def get_cached_result(db: Session, translated_query: str, service: str, database_name: str) -> QueryCache | None:
    """Return the most recent completed submission for this translated query
    against this database, if any.

    The same translated_query can have been submitted many times (at different
    milestones), so this isn't a unique lookup — it filters on translated_query_hash
    + service + database_name and takes the newest completed row. database_name is
    part of the key because the same translated query means different things on
    different databases; max_number_of_results/time_out_limit are just execution
    knobs and don't affect what the correct answer is, so they're left out of the
    lookup. This matches ix_query_cache_lookup (partial index on status='completed',
    ordered by submitted_at DESC), so it's a single index scan with no sort step,
    not a table scan.
    """
    stmt = (
        select(QueryCache)
        .where(
            QueryCache.translated_query_hash == _hash(translated_query),
            QueryCache.service == service,
            QueryCache.database_name == database_name,
            QueryCache.status == "completed",
        )
        .order_by(QueryCache.submitted_at.desc())
        .limit(1)
    )
    return db.execute(stmt).scalar_one_or_none()


def record_query_submission(
    db: Session,
    *,
    service: str,
    user_query: str,
    database_name: str,
    max_number_of_results: int,
    time_out_limit: int,
    translated_query: str,
    result: dict | None = None,
    status: str = "pending",
    error_message: str | None = None,
) -> QueryCache:
    """Always inserts a new row — one per submission, even if translated_query
    duplicates an earlier row. Callers should check get_cached_result() first if
    they want to skip re-executing an already-answered translated query.
    """
    row = QueryCache(
        service=service,
        user_query=user_query,
        database_name=database_name,
        max_number_of_results=max_number_of_results,
        time_out_limit=time_out_limit,
        translated_query=translated_query,
        result=result,
        status=status,
        error_message=error_message,
    )
    db.add(row)
    db.commit()
    db.refresh(row)
    return row
