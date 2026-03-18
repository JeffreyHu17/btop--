from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlmodel import select

from app.deps import SessionDep, get_metrics_write_api_key
from app.models import Agent
from app.routers.metrics import ingest_metrics
from app.schemas import AgentRemoteConfigRead, MetricsIngestResponse

router = APIRouter(prefix="/api", tags=["compat"])


@router.get("/ping")
def ping() -> dict[str, str]:
    return {"status": "ok"}


@router.get("/auth/status")
def auth_status(_api_key=Depends(get_metrics_write_api_key)) -> dict[str, bool]:
    return {"authenticated": True}


@router.get("/agent/config", response_model=AgentRemoteConfigRead)
def agent_config(
    session: SessionDep,
    api_key=Depends(get_metrics_write_api_key),
    hostname: str = Query(min_length=1),
) -> AgentRemoteConfigRead:
    row = session.exec(
        select(Agent).where(Agent.hostname == hostname, Agent.owner_user_id == api_key.owner_user_id)
    ).first()
    if row is None:
        return AgentRemoteConfigRead(
            hostname=hostname,
            display_name=hostname,
            collection_interval_ms=1000,
            enable_gpu=True,
        )
    return AgentRemoteConfigRead(
        hostname=row.hostname,
        display_name=row.display_name,
        collection_interval_ms=row.desired_collection_interval_ms,
        enable_gpu=row.desired_enable_gpu,
    )


@router.post("/metrics", response_model=MetricsIngestResponse)
def ingest_metrics_compat(
    payload: dict[str, Any],
    session: SessionDep,
    api_key=Depends(get_metrics_write_api_key),
) -> MetricsIngestResponse:
    return ingest_metrics(payload=payload, session=session, api_key=api_key)
