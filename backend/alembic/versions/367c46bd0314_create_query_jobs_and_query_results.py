"""create query_jobs and query_results

Revision ID: 367c46bd0314
Revises: 
Create Date: 2026-08-12 20:46:28.592232

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


# revision identifiers, used by Alembic.
revision: str = '367c46bd0314'
down_revision: Union[str, Sequence[str], None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
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


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_table("query_results")
    op.drop_table("query_jobs")
