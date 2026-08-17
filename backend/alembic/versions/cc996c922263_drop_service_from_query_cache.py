"""drop service from query_cache

Revision ID: cc996c922263
Revises: fb2e93712252
Create Date: 2026-08-17 20:59:54.237738

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = 'cc996c922263'
down_revision: Union[str, Sequence[str], None] = 'fb2e93712252'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    op.drop_index("ix_query_cache_lookup", table_name="query_cache")
    op.drop_constraint("query_cache_service_check", "query_cache", type_="check")
    op.drop_column("query_cache", "service")
    op.execute(
        """
        CREATE INDEX ix_query_cache_lookup
        ON query_cache (translated_query_hash, database_name, submitted_at DESC)
        WHERE status = 'completed'
        """
    )


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_index("ix_query_cache_lookup", table_name="query_cache")
    op.add_column("query_cache", sa.Column("service", sa.Text(), nullable=True))
    op.create_check_constraint(
        "query_cache_service_check", "query_cache", "service IN ('ring-rpq', 'xpathringrpq')"
    )
    op.execute(
        """
        CREATE INDEX ix_query_cache_lookup
        ON query_cache (translated_query_hash, service, database_name, submitted_at DESC)
        WHERE status = 'completed'
        """
    )
