from sqlalchemy.orm import Session

from .models import TranslationFailure


def record_translation_failure(
    db: Session,
    *,
    user_query: str,
    database_name: str,
    max_number_of_results: int,
    time_out_limit: int,
    error_message: str | None = None,
) -> TranslationFailure:
    """Log a user query the Java translator rejected. Write-only — nothing reads
    this table back during a request, so there's no corresponding lookup helper.
    """
    row = TranslationFailure(
        user_query=user_query,
        database_name=database_name,
        max_number_of_results=max_number_of_results,
        time_out_limit=time_out_limit,
        error_message=error_message,
    )
    db.add(row)
    db.commit()
    db.refresh(row)
    return row
