import json
from collections.abc import Generator
from datetime import UTC, datetime
from typing import Annotated

from fastapi import Depends, Header, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlmodel import Session, select

from app.core.security import decode_access_token, hash_api_key
from app.db import get_session
from app.models import ApiKey, User

bearer_scheme = HTTPBearer(auto_error=False)


def session_dep() -> Generator[Session, None, None]:
    yield from get_session()


SessionDep = Annotated[Session, Depends(session_dep)]


def get_current_user(
    session: SessionDep,
    credentials: Annotated[HTTPAuthorizationCredentials | None, Depends(bearer_scheme)],
) -> User:
    if credentials is None or credentials.scheme.lower() != "bearer":
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing bearer token")
    try:
        payload = decode_access_token(credentials.credentials)
        user_id = payload.get("sub")
    except Exception as exc:  # pragma: no cover - explicit auth guard
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token") from exc

    user = session.get(User, user_id)
    if user is None or not user.is_active:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found")
    return user


CurrentUser = Annotated[User, Depends(get_current_user)]


def parse_scopes(scopes: str) -> set[str]:
    normalized = scopes.replace(",", " ")
    return {item.strip() for item in normalized.split() if item.strip()}


def get_ingest_api_key(
    session: SessionDep,
    x_api_key: Annotated[str | None, Header(alias="X-API-Key")] = None,
    authorization: Annotated[str | None, Header(alias="Authorization")] = None,
) -> ApiKey:
    raw_key = x_api_key
    if not raw_key and authorization and authorization.startswith("Bearer "):
        raw_key = authorization.removeprefix("Bearer ").strip()

    if not raw_key:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing API key")

    key = session.exec(select(ApiKey).where(ApiKey.hashed_key == hash_api_key(raw_key))).first()
    if key is None or not key.is_active or key.revoked_at is not None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid API key")

    key.last_used_at = datetime.now(UTC)
    session.add(key)
    session.commit()
    session.refresh(key)
    return key


def get_metrics_write_api_key(api_key: Annotated[ApiKey, Depends(get_ingest_api_key)]) -> ApiKey:
    if "metrics:write" not in parse_scopes(api_key.scopes):
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="API key is missing metrics:write scope")
    return api_key


def parse_payload(payload_json: str | None) -> dict | None:
    if not payload_json:
        return None
    try:
        return json.loads(payload_json)
    except json.JSONDecodeError:
        return None
