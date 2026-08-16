from sqlalchemy import CheckConstraint, Column, Computed, Index, BigInteger, Integer, LargeBinary, Text, TIMESTAMP, text
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    pass


class QueryCache(Base):
    """One row per query submission (not deduplicated — the same translated_query
    can appear on multiple rows, submitted at different times). Doubles as the
    result cache: ix_query_cache_lookup lets a lookup by (translated_query_hash,
    service, database_name) find the most recent completed result for a translated
    query in a single index scan.
    """

    __tablename__ = "query_cache"

    id = Column(BigInteger, primary_key=True, autoincrement=True)
    service = Column(Text, nullable=False)
    user_query = Column(Text, nullable=False)
    database_name = Column(Text, nullable=False)
    max_number_of_results = Column(Integer, nullable=False)
    time_out_limit = Column(Integer, nullable=False)
    translated_query = Column(Text, nullable=False)
    translated_query_hash = Column(
        LargeBinary, Computed("sha256(translated_query::bytea)", persisted=True), nullable=False
    )
    status = Column(Text, nullable=False, server_default="pending")
    result = Column(JSONB, nullable=True)
    error_message = Column(Text, nullable=True)
    submitted_at = Column(TIMESTAMP(timezone=True), nullable=False, server_default=text("now()"))
    completed_at = Column(TIMESTAMP(timezone=True), nullable=True)

    __table_args__ = (
        CheckConstraint("service IN ('ring-rpq', 'xpathringrpq')", name="query_cache_service_check"),
        CheckConstraint("status IN ('pending', 'completed', 'failed')", name="query_cache_status_check"),
        Index(
            "ix_query_cache_lookup",
            "translated_query_hash",
            "service",
            "database_name",
            text("submitted_at DESC"),
            postgresql_where=text("status = 'completed'"),
        ),
    )


class TranslationFailure(Base):
    """One row per user query the Java translator couldn't turn into a valid
    translated query. Purely a log — there's no later fast lookup against this
    table, so unlike QueryCache it carries no hash column or extra index.
    """

    __tablename__ = "translation_failures"

    id = Column(BigInteger, primary_key=True, autoincrement=True)
    user_query = Column(Text, nullable=False)
    database_name = Column(Text, nullable=False)
    max_number_of_results = Column(Integer, nullable=False)
    time_out_limit = Column(Integer, nullable=False)
    error_message = Column(Text, nullable=True)
    submitted_at = Column(TIMESTAMP(timezone=True), nullable=False, server_default=text("now()"))
