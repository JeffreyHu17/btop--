import json

from sqlmodel import Session, select

from app.core.config import get_settings
from app.core.security import hash_password
from app.models import Dashboard, User


def ensure_bootstrap_admin(session: Session) -> None:
    settings = get_settings()
    existing = session.exec(select(User).where(User.username == settings.bootstrap_admin_username)).first()
    if existing is not None:
        return

    user = User(
        username=settings.bootstrap_admin_username,
        password_hash=hash_password(settings.bootstrap_admin_password),
        is_active=True,
        is_superuser=True,
    )
    session.add(user)
    session.commit()
    session.refresh(user)

    dashboard = Dashboard(
        owner_user_id=user.id,
        name="Default",
        is_default=True,
        layout_json=json.dumps(
            {
                "panels": [
                    {"id": "fleet", "type": "agent-list", "x": 0, "y": 0, "w": 4, "h": 10},
                    {"id": "cpu", "type": "chart", "metric": "cpu.usage_percent", "x": 4, "y": 0, "w": 8, "h": 5},
                    {"id": "memory", "type": "chart", "metric": "memory.used_bytes", "x": 4, "y": 5, "w": 8, "h": 5},
                ]
            }
        ),
    )
    session.add(dashboard)
    session.commit()
