from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str
    xpathringrpq_url: str
    frontend_origin: str

    class Config:
        env_file = ".env"


settings = Settings()
