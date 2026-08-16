"""create translation_failures

Revision ID: fb2e93712252
Revises: 8f2c1a4e9b3d
Create Date: 2026-08-16 21:50:44.330533

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = 'fb2e93712252'
down_revision: Union[str, Sequence[str], None] = '8f2c1a4e9b3d'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    # Derived from query_cache but pared down: every row here is already a failure
    # (no status/result to track) and there's no cache-style re-lookup by this
    # table, so no hash column or index beyond the primary key.
    op.create_table(
        "translation_failures",
        sa.Column("id", sa.BigInteger(), primary_key=True, autoincrement=True),
        sa.Column("user_query", sa.Text(), nullable=False),
        sa.Column("database_name", sa.Text(), nullable=False),
        sa.Column("max_number_of_results", sa.Integer(), nullable=False),
        sa.Column("time_out_limit", sa.Integer(), nullable=False),
        sa.Column("error_message", sa.Text(), nullable=True),
        sa.Column("submitted_at", sa.TIMESTAMP(timezone=True), nullable=False, server_default=sa.text("now()")),
    )
    op.execute("GRANT SELECT, INSERT ON translation_failures TO ringrpq")
    op.execute("GRANT USAGE, SELECT ON SEQUENCE translation_failures_id_seq TO ringrpq")


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_table("translation_failures")
