from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class LoginRequest(BaseModel):
    username: str
    password: str


class UserRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    username: str
    is_active: bool
    is_superuser: bool
    created_at: datetime


class UserCreateRequest(BaseModel):
    username: str = Field(min_length=1, max_length=100)
    password: str = Field(min_length=8, max_length=256)
    is_active: bool = True
    is_superuser: bool = False


class UserUpdateRequest(BaseModel):
    password: str | None = Field(default=None, min_length=8, max_length=256)
    is_active: bool | None = None
    is_superuser: bool | None = None


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    user: UserRead


class ApiKeyCreateRequest(BaseModel):
    label: str = Field(min_length=1, max_length=100)
    scopes: str = "metrics:write"


class ApiKeyRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    label: str
    key_prefix: str
    scopes: str
    is_active: bool
    created_at: datetime
    last_used_at: datetime | None = None
    revoked_at: datetime | None = None


class ApiKeyCreateResponse(BaseModel):
    api_key: str
    key: ApiKeyRead


class DashboardRead(BaseModel):
    id: str
    name: str
    is_default: bool
    layout: dict[str, Any]
    updated_at: datetime


class DashboardUpdateRequest(BaseModel):
    name: str = "Default"
    layout: dict[str, Any]


class AgentSummary(BaseModel):
    id: str
    hostname: str
    display_name: str | None
    owner_user_id: str | None = None
    status: str
    desired_collection_interval_ms: int
    desired_enable_gpu: bool
    last_seen_at: datetime | None
    latest_payload: dict[str, Any] | None
    latest_payload_at: datetime | None


class AgentSettingsRead(BaseModel):
    agent_id: str
    hostname: str
    display_name: str | None = None
    desired_collection_interval_ms: int
    desired_enable_gpu: bool


class AgentSettingsUpdateRequest(BaseModel):
    display_name: str | None = Field(default=None, max_length=120)
    desired_collection_interval_ms: int | None = Field(default=None, ge=250, le=60 * 60 * 1000)
    desired_enable_gpu: bool | None = None


class AgentRemoteConfigRead(BaseModel):
    hostname: str
    display_name: str | None = None
    collection_interval_ms: int
    enable_gpu: bool


class AgentHistoryResponse(BaseModel):
    agent_id: str
    points: list[dict[str, Any]]


class AgentTrendPointRead(BaseModel):
    timestamp: datetime
    cpu_usage_percent: float | None = None
    memory_used_bytes: int | None = None
    memory_total_bytes: int | None = None
    gpu_utilization_percent: float | None = None
    process_count: int | None = None
    load_average_1min: float | None = None


class AgentTrendRead(BaseModel):
    agent_id: str
    points: list[AgentTrendPointRead]


class MetricsIngestResponse(BaseModel):
    accepted: bool = True
    agent_id: str
    received_at: datetime
