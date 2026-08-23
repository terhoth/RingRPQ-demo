"""add translation_warning_message to query_cache

Revision ID: 59c2cbf50cd4
Revises: 152fc6894cd4
Create Date: 2026-08-21 00:00:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '59c2cbf50cd4'
down_revision: Union[str, Sequence[str], None] = '152fc6894cd4'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    op.add_column("query_cache", sa.Column("translation_warning_message", sa.Text(), nullable=True))


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_column("query_cache", "translation_warning_message")
