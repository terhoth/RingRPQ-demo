"""add running to query_cache status

Revision ID: 152fc6894cd4
Revises: cc996c922263
Create Date: 2026-08-17 21:01:35.667810

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '152fc6894cd4'
down_revision: Union[str, Sequence[str], None] = 'cc996c922263'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    op.drop_constraint("query_cache_status_check", "query_cache", type_="check")
    op.create_check_constraint(
        "query_cache_status_check",
        "query_cache",
        "status IN ('pending', 'running', 'completed', 'failed')",
    )


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_constraint("query_cache_status_check", "query_cache", type_="check")
    op.create_check_constraint(
        "query_cache_status_check",
        "query_cache",
        "status IN ('pending', 'completed', 'failed')",
    )
