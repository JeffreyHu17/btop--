from functools import lru_cache
from pathlib import Path

from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_DATABASE_PATH = PROJECT_ROOT / "data" / "btop-control-plane.db"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="BTOP_", env_file=".env", extra="ignore")

    app_name: str = "btop Control Plane"
    env: str = "development"
    api_prefix: str = "/api/v1"
    database_url: str = f"sqlite:///{DEFAULT_DATABASE_PATH}"
    jwt_secret: str = "change-this-secret"
    jwt_algorithm: str = "HS256"
    jwt_expire_minutes: int = 720
    frontend_dist_dir: Path = PROJECT_ROOT / "frontend" / "dist"
    bootstrap_admin_username: str = "admin"
    bootstrap_admin_password: str = "admin123456"
    agent_stale_after_seconds: int = 90
    cors_origins: list[str] = ["http://localhost:3001", "http://127.0.0.1:3001"]

    @field_validator("cors_origins", mode="before")
    @classmethod
    def split_cors_origins(cls, value: str | list[str]) -> list[str]:
        if isinstance(value, str):
            return [item.strip() for item in value.split(",") if item.strip()]
        return value


@lru_cache
def get_settings() -> Settings:
    return Settings()
