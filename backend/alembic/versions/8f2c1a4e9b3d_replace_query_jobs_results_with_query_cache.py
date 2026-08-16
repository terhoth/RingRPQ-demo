"""replace query_jobs/query_results with query_cache

Revision ID: 8f2c1a4e9b3d
Revises: 367c46bd0314
Create Date: 2026-08-14 00:00:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


# revision identifiers, used by Alembic.
revision: str = '8f2c1a4e9b3d'
down_revision: Union[str, Sequence[str], None] = '367c46bd0314'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    op.drop_table("query_results")
    op.drop_table("query_jobs")

    op.create_table(
        "query_cache",
        sa.Column("id", sa.BigInteger(), primary_key=True, autoincrement=True),
        sa.Column("service", sa.Text(), nullable=False),
        sa.Column("user_query", sa.Text(), nullable=False),
        sa.Column("database_name", sa.Text(), nullable=False),
        sa.Column("max_number_of_results", sa.Integer(), nullable=False),
        sa.Column("time_out_limit", sa.Integer(), nullable=False),
        sa.Column("translated_query", sa.Text(), nullable=False),
        # sha256(translated_query) computed by Postgres itself (built in since PG14) so
        # every writer — backend, Ring-RPQ's pg-bridge — gets a consistent, fixed-size
        # cache key without needing to hash it client-side.
        sa.Column(
            "translated_query_hash",
            postgresql.BYTEA(),
            sa.Computed("sha256(translated_query::bytea)", persisted=True),
            nullable=False,
        ),
        sa.Column("status", sa.Text(), nullable=False, server_default="pending"),
        sa.Column("result", postgresql.JSONB(), nullable=True),
        sa.Column("error_message", sa.Text(), nullable=True),
        sa.Column("submitted_at", sa.TIMESTAMP(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.Column("completed_at", sa.TIMESTAMP(timezone=True), nullable=True),
        sa.CheckConstraint("service IN ('ring-rpq', 'xpathringrpq')", name="query_cache_service_check"),
        sa.CheckConstraint("status IN ('pending', 'completed', 'failed')", name="query_cache_status_check"),
    )
    # Same translated_query can legitimately appear on multiple rows (submitted at
    # different times), so this is a plain index, not a unique one. It's partial
    # (completed rows only) and pre-sorted by submitted_at DESC so the "is there
    # already a result for this translated query?" lookup — equality on
    # (translated_query_hash, service, database_name), newest first, LIMIT 1 — is a
    # pure index scan with no extra sort step. database_name is part of the key
    # because the same translated query means different things on different
    # databases; max_number_of_results/time_out_limit are just execution knobs and
    # don't affect what the correct answer is, so they're left out of the key.
    op.execute(
        """
        CREATE INDEX ix_query_cache_lookup
        ON query_cache (translated_query_hash, service, database_name, submitted_at DESC)
        WHERE status = 'completed'
        """
    )
    op.execute("GRANT SELECT, INSERT, UPDATE ON query_cache TO ringrpq")
    op.execute("GRANT USAGE, SELECT ON SEQUENCE query_cache_id_seq TO ringrpq")


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_table("query_cache")

    op.create_table(
        "query_jobs",
        sa.Column("id", sa.BigInteger(), primary_key=True, autoincrement=True),
        sa.Column("service", sa.Text(), nullable=False),
        sa.Column("query_text", sa.Text(), nullable=False),
        sa.Column("status", sa.Text(), nullable=False, server_default="pending"),
        sa.Column("submitted_at", sa.TIMESTAMP(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.Column("completed_at", sa.TIMESTAMP(timezone=True), nullable=True),
        sa.CheckConstraint("service IN ('ring-rpq', 'xpathringrpq')", name="query_jobs_service_check"),
    )
    op.create_table(
        "query_results",
        sa.Column("id", sa.BigInteger(), primary_key=True, autoincrement=True),
        sa.Column("job_id", sa.BigInteger(), sa.ForeignKey("query_jobs.id"), nullable=False),
        sa.Column("payload", postgresql.JSONB(), nullable=False),
    )
    op.execute("GRANT SELECT, INSERT, UPDATE ON query_jobs, query_results TO ringrpq")
