import json
from datetime import UTC, datetime

from fastapi import APIRouter
from sqlmodel import select

from app.deps import CurrentUser, SessionDep
from app.models import Dashboard
from app.schemas import DashboardRead, DashboardUpdateRequest

router = APIRouter(prefix="/dashboards", tags=["dashboards"])


def to_dashboard_read(row: Dashboard) -> DashboardRead:
    return DashboardRead(
        id=row.id,
        name=row.name,
        is_default=row.is_default,
        layout=json.loads(row.layout_json),
        updated_at=row.updated_at,
    )


def get_or_create_default_dashboard(session: SessionDep, user_id: str) -> Dashboard:
    row = session.exec(
        select(Dashboard).where(Dashboard.owner_user_id == user_id, Dashboard.is_default == True)
    ).first()
    if row is not None:
        return row

    row = Dashboard(owner_user_id=user_id, name="Default", is_default=True)
    session.add(row)
    session.commit()
    session.refresh(row)
    return row


@router.get("/default", response_model=DashboardRead)
def read_default_dashboard(current_user: CurrentUser, session: SessionDep) -> DashboardRead:
    return to_dashboard_read(get_or_create_default_dashboard(session, current_user.id))


@router.put("/default", response_model=DashboardRead)
def update_default_dashboard(
    payload: DashboardUpdateRequest,
    current_user: CurrentUser,
    session: SessionDep,
) -> DashboardRead:
    row = get_or_create_default_dashboard(session, current_user.id)
    row.name = payload.name
    row.layout_json = json.dumps(payload.layout)
    row.updated_at = datetime.now(UTC)
    session.add(row)
    session.commit()
    session.refresh(row)
    return to_dashboard_read(row)
