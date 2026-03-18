from __future__ import annotations

from datetime import UTC, datetime
from typing import Optional
from uuid import uuid4

from sqlalchemy import UniqueConstraint
from sqlmodel import Field, SQLModel


def utcnow() -> datetime:
    return datetime.now(UTC)


class User(SQLModel, table=True):
    id: str = Field(default_factory=lambda: str(uuid4()), primary_key=True)
    username: str = Field(index=True, unique=True)
    password_hash: str
    is_active: bool = True
    is_superuser: bool = True
    created_at: datetime = Field(default_factory=utcnow, nullable=False)


class ApiKey(SQLModel, table=True):
    id: str = Field(default_factory=lambda: str(uuid4()), primary_key=True)
    owner_user_id: str = Field(index=True, foreign_key="user.id")
    label: str
    key_prefix: str = Field(index=True)
    hashed_key: str = Field(index=True, unique=True)
    scopes: str = "metrics:write"
    is_active: bool = True
    created_at: datetime = Field(default_factory=utcnow, nullable=False)
    last_used_at: Optional[datetime] = Field(default=None, nullable=True)
    revoked_at: Optional[datetime] = Field(default=None, nullable=True)


class Agent(SQLModel, table=True):
    __table_args__ = (UniqueConstraint("owner_user_id", "hostname", name="uq_agent_owner_hostname"),)

    id: str = Field(default_factory=lambda: str(uuid4()), primary_key=True)
    hostname: str = Field(index=True)
    owner_user_id: Optional[str] = Field(default=None, index=True, foreign_key="user.id")
    last_api_key_id: Optional[str] = Field(default=None, index=True, foreign_key="apikey.id")
    display_name: Optional[str] = None
    status: str = Field(default="offline", index=True)
    desired_collection_interval_ms: int = Field(default=1000, nullable=False)
    desired_enable_gpu: bool = True
    last_seen_at: Optional[datetime] = Field(default=None, nullable=True)
    latest_payload_json: Optional[str] = Field(default=None, nullable=True)
    latest_payload_at: Optional[datetime] = Field(default=None, nullable=True)
    created_at: datetime = Field(default_factory=utcnow, nullable=False)
    updated_at: datetime = Field(default_factory=utcnow, nullable=False)


class MetricSample(SQLModel, table=True):
    id: str = Field(default_factory=lambda: str(uuid4()), primary_key=True)
    agent_id: str = Field(index=True, foreign_key="agent.id")
    hostname: str = Field(index=True)
    collected_at: datetime = Field(index=True)
    received_at: datetime = Field(default_factory=utcnow, index=True, nullable=False)
    payload_json: str


class Dashboard(SQLModel, table=True):
    id: str = Field(default_factory=lambda: str(uuid4()), primary_key=True)
    owner_user_id: str = Field(index=True, foreign_key="user.id")
    name: str = "Default"
    layout_json: str = '{"panels":[]}'
    is_default: bool = True
    created_at: datetime = Field(default_factory=utcnow, nullable=False)
    updated_at: datetime = Field(default_factory=utcnow, nullable=False)
