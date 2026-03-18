from collections import defaultdict
from datetime import UTC, datetime, timedelta
import json
from typing import Any

from fastapi import APIRouter, HTTPException, Query, Response, status
from sqlmodel import select

from app.core.config import get_settings
from app.deps import CurrentUser, SessionDep, parse_payload
from app.models import Agent, MetricSample
from app.schemas import (
    AgentHistoryResponse,
    AgentSettingsRead,
    AgentSettingsUpdateRequest,
    AgentSummary,
    AgentTrendPointRead,
    AgentTrendRead,
)

router = APIRouter(prefix="/agents", tags=["agents"])
settings = get_settings()


def normalize_timestamp(value: datetime) -> datetime:
    if value.tzinfo is None:
        return value.replace(tzinfo=UTC)
    return value.astimezone(UTC)


def resolve_agent_status(agent: Agent) -> str:
    if agent.last_seen_at is None:
        return "offline"
    age = datetime.now(UTC) - normalize_timestamp(agent.last_seen_at)
    if age > timedelta(seconds=settings.agent_stale_after_seconds):
        return "offline"
    return agent.status or "online"


def slim_agent_payload(payload: dict[str, Any] | None) -> dict[str, Any] | None:
    if payload is None:
        return None
    summary_payload = json.loads(json.dumps(payload))
    processes = summary_payload.get("processes")
    if isinstance(processes, dict):
        processes.pop("all_processes", None)
    return summary_payload


def to_agent_summary(row: Agent, *, include_all_processes: bool = False) -> AgentSummary:
    payload = parse_payload(row.latest_payload_json)
    if not include_all_processes:
        payload = slim_agent_payload(payload)
    return AgentSummary(
        id=row.id,
        hostname=row.hostname,
        display_name=row.display_name,
        owner_user_id=row.owner_user_id,
        status=resolve_agent_status(row),
        desired_collection_interval_ms=row.desired_collection_interval_ms,
        desired_enable_gpu=row.desired_enable_gpu,
        last_seen_at=row.last_seen_at,
        latest_payload=payload,
        latest_payload_at=row.latest_payload_at,
    )


def to_agent_settings(row: Agent) -> AgentSettingsRead:
    return AgentSettingsRead(
        agent_id=row.id,
        hostname=row.hostname,
        display_name=row.display_name,
        desired_collection_interval_ms=row.desired_collection_interval_ms,
        desired_enable_gpu=row.desired_enable_gpu,
    )


def resolve_agent_for_user(session: SessionDep, current_user: CurrentUser, agent_id: str) -> Agent:
    row = session.get(Agent, agent_id)
    if row is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Agent not found")
    if current_user.is_superuser or row.owner_user_id == current_user.id:
        return row
    raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Agent not found")


def visible_agents_statement(current_user: CurrentUser):
    statement = select(Agent).order_by(Agent.updated_at.desc())
    if not current_user.is_superuser:
        statement = statement.where(Agent.owner_user_id == current_user.id)
    return statement


def payload_number(payload: dict[str, Any] | None, *path: str) -> float | None:
    current: Any = payload
    for segment in path:
        if not isinstance(current, dict):
            return None
        current = current.get(segment)

    if isinstance(current, (int, float)):
        return float(current)
    return None


def build_trend_point(sample: MetricSample) -> AgentTrendPointRead | None:
    payload = parse_payload(sample.payload_json)
    if payload is None:
        return None

    gpu_utilization = None
    raw_gpus = payload.get("gpus")
    if isinstance(raw_gpus, list) and raw_gpus:
        first_gpu = raw_gpus[0]
        if isinstance(first_gpu, dict):
            raw_utilization = first_gpu.get("utilization_percent")
            if isinstance(raw_utilization, (int, float)):
                gpu_utilization = float(raw_utilization)

    memory_used = payload_number(payload, "memory", "used_bytes")
    memory_total = payload_number(payload, "memory", "total_bytes")

    return AgentTrendPointRead(
        timestamp=sample.collected_at,
        cpu_usage_percent=payload_number(payload, "cpu", "usage_percent"),
        memory_used_bytes=int(memory_used) if memory_used is not None else None,
        memory_total_bytes=int(memory_total) if memory_total is not None else None,
        gpu_utilization_percent=gpu_utilization,
        process_count=int(process_count) if (process_count := payload_number(payload, "processes", "total_processes")) is not None else None,
        load_average_1min=payload_number(payload, "processes", "load_average_1min"),
    )


def downsample_points(points: list[AgentTrendPointRead], limit: int) -> list[AgentTrendPointRead]:
    if len(points) <= limit:
        return points

    if limit <= 1:
        return [points[-1]]

    step = (len(points) - 1) / (limit - 1)
    indexes = sorted({round(step * index) for index in range(limit)})
    return [points[index] for index in indexes]


@router.get("", response_model=list[AgentSummary])
def list_agents(current_user: CurrentUser, session: SessionDep) -> list[AgentSummary]:
    rows = session.exec(visible_agents_statement(current_user)).all()
    return [to_agent_summary(row) for row in rows]


@router.get("/trends", response_model=list[AgentTrendRead])
def list_agent_trends(
    current_user: CurrentUser,
    session: SessionDep,
    minutes: int = Query(default=60, ge=5, le=24 * 60),
    limit: int = Query(default=24, ge=6, le=120),
) -> list[AgentTrendRead]:
    rows = session.exec(visible_agents_statement(current_user)).all()
    if not rows:
        return []

    agent_ids = [row.id for row in rows]
    cutoff = datetime.now(UTC) - timedelta(minutes=minutes)
    samples = session.exec(
        select(MetricSample)
        .where(MetricSample.agent_id.in_(agent_ids), MetricSample.collected_at >= cutoff)
        .order_by(MetricSample.agent_id.asc(), MetricSample.collected_at.asc())
    ).all()

    grouped: dict[str, list[AgentTrendPointRead]] = defaultdict(list)
    for sample in samples:
        point = build_trend_point(sample)
        if point is not None:
            grouped[sample.agent_id].append(point)

    return [
        AgentTrendRead(agent_id=row.id, points=downsample_points(grouped.get(row.id, []), limit))
        for row in rows
    ]


@router.get("/{agent_id}", response_model=AgentSummary)
def read_agent(agent_id: str, current_user: CurrentUser, session: SessionDep) -> AgentSummary:
    return to_agent_summary(resolve_agent_for_user(session, current_user, agent_id), include_all_processes=True)


@router.get("/{agent_id}/settings", response_model=AgentSettingsRead)
def read_agent_settings(agent_id: str, current_user: CurrentUser, session: SessionDep) -> AgentSettingsRead:
    return to_agent_settings(resolve_agent_for_user(session, current_user, agent_id))


@router.put("/{agent_id}/settings", response_model=AgentSettingsRead)
def update_agent_settings(
    agent_id: str,
    payload: AgentSettingsUpdateRequest,
    current_user: CurrentUser,
    session: SessionDep,
) -> AgentSettingsRead:
    row = resolve_agent_for_user(session, current_user, agent_id)

    if payload.display_name is not None:
        row.display_name = payload.display_name.strip() or row.hostname
    if payload.desired_collection_interval_ms is not None:
        row.desired_collection_interval_ms = payload.desired_collection_interval_ms
    if payload.desired_enable_gpu is not None:
        row.desired_enable_gpu = payload.desired_enable_gpu

    row.updated_at = datetime.now(UTC)
    session.add(row)
    session.commit()
    session.refresh(row)
    return to_agent_settings(row)


@router.delete("/{agent_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_agent(agent_id: str, current_user: CurrentUser, session: SessionDep) -> Response:
    row = resolve_agent_for_user(session, current_user, agent_id)

    samples = session.exec(select(MetricSample).where(MetricSample.agent_id == agent_id)).all()
    for sample in samples:
        session.delete(sample)

    session.delete(row)
    session.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)


@router.get("/{agent_id}/history", response_model=AgentHistoryResponse)
def read_agent_history(
    agent_id: str,
    current_user: CurrentUser,
    session: SessionDep,
    minutes: int = Query(default=60, ge=1, le=24 * 60),
) -> AgentHistoryResponse:
    row = resolve_agent_for_user(session, current_user, agent_id)

    cutoff = datetime.now(UTC) - timedelta(minutes=minutes)
    samples = session.exec(
        select(MetricSample)
        .where(MetricSample.agent_id == agent_id, MetricSample.collected_at >= cutoff)
        .order_by(MetricSample.collected_at.asc())
    ).all()
    points: list[dict] = []
    for sample in samples:
        payload = parse_payload(sample.payload_json)
        if payload is None:
            continue
        payload.setdefault("timestamp", sample.collected_at.isoformat())
        payload.setdefault("received_at", sample.received_at.isoformat())
        points.append(payload)
    return AgentHistoryResponse(agent_id=agent_id, points=points)
