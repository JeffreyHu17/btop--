import json
from datetime import UTC, datetime
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import or_
from sqlmodel import select

from app.deps import SessionDep, get_metrics_write_api_key
from app.models import Agent, MetricSample
from app.schemas import MetricsIngestResponse

router = APIRouter(prefix="/metrics", tags=["metrics"])


def parse_recorded_at(payload: dict[str, Any]) -> datetime:
    raw_timestamp = payload.get("timestamp")
    if isinstance(raw_timestamp, str) and raw_timestamp.strip():
        try:
            return datetime.fromisoformat(raw_timestamp.replace("Z", "+00:00")).astimezone(UTC)
        except ValueError:
            pass
    return datetime.now(UTC)


def slim_history_payload(payload: dict[str, Any]) -> dict[str, Any]:
    history_payload = json.loads(json.dumps(payload))
    processes = history_payload.get("processes")
    if isinstance(processes, dict):
        processes.pop("all_processes", None)
    return history_payload


@router.post("", response_model=MetricsIngestResponse, status_code=status.HTTP_202_ACCEPTED)
@router.post("/ingest", response_model=MetricsIngestResponse, status_code=status.HTTP_202_ACCEPTED)
def ingest_metrics(
    payload: dict[str, Any],
    session: SessionDep,
    api_key=Depends(get_metrics_write_api_key),
) -> MetricsIngestResponse:
    hostname = payload.get("hostname")
    if not isinstance(hostname, str) or not hostname.strip():
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="hostname is required")

    hostname = hostname.strip()
    agent = session.exec(
        select(Agent).where(
            Agent.hostname == hostname,
            or_(Agent.owner_user_id == api_key.owner_user_id, Agent.owner_user_id.is_(None)),
        )
    ).first()
    collected_at = parse_recorded_at(payload)
    received_at = datetime.now(UTC)
    latest_payload_json = json.dumps(payload)
    history_payload_json = json.dumps(slim_history_payload(payload))

    if agent is None:
        agent = Agent(
            hostname=hostname,
            display_name=hostname,
            owner_user_id=api_key.owner_user_id,
            last_api_key_id=api_key.id,
        )

    agent.owner_user_id = api_key.owner_user_id
    agent.last_api_key_id = api_key.id
    agent.status = "online"
    agent.latest_payload_json = latest_payload_json
    agent.last_seen_at = received_at
    agent.latest_payload_at = collected_at
    agent.updated_at = received_at
    session.add(agent)

    sample = session.exec(
        select(MetricSample).where(
            MetricSample.agent_id == agent.id,
            MetricSample.collected_at == collected_at,
            MetricSample.payload_json == history_payload_json,
        )
    ).first()
    if sample is None:
        sample = MetricSample(
            agent_id=agent.id,
            hostname=hostname,
            collected_at=collected_at,
            received_at=received_at,
            payload_json=history_payload_json,
        )
        session.add(sample)

    session.commit()
    session.refresh(agent)
    session.refresh(sample)

    return MetricsIngestResponse(agent_id=agent.id, received_at=sample.received_at)
