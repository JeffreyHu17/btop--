from fastapi import APIRouter, HTTPException, Response, status
from sqlmodel import select

from app.core.security import generate_api_key
from app.deps import CurrentUser, SessionDep
from app.models import Agent, ApiKey
from app.schemas import ApiKeyCreateRequest, ApiKeyCreateResponse, ApiKeyRead

router = APIRouter(prefix="/api-keys", tags=["api-keys"])


@router.get("", response_model=list[ApiKeyRead])
def list_api_keys(current_user: CurrentUser, session: SessionDep) -> list[ApiKeyRead]:
    rows = session.exec(select(ApiKey).where(ApiKey.owner_user_id == current_user.id)).all()
    return [ApiKeyRead.model_validate(row) for row in rows]


@router.post("", response_model=ApiKeyCreateResponse, status_code=status.HTTP_201_CREATED)
def create_api_key(
    payload: ApiKeyCreateRequest,
    current_user: CurrentUser,
    session: SessionDep,
) -> ApiKeyCreateResponse:
    raw_key, hashed_key, key_prefix = generate_api_key()
    row = ApiKey(
        owner_user_id=current_user.id,
        label=payload.label,
        scopes=payload.scopes,
        key_prefix=key_prefix,
        hashed_key=hashed_key,
    )
    session.add(row)
    session.commit()
    session.refresh(row)
    return ApiKeyCreateResponse(api_key=raw_key, key=ApiKeyRead.model_validate(row))


@router.post("/{key_id}/revoke", response_model=ApiKeyRead)
def revoke_api_key(key_id: str, current_user: CurrentUser, session: SessionDep) -> ApiKeyRead:
    row = session.get(ApiKey, key_id)
    if row is None or row.owner_user_id != current_user.id:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="API key not found")

    if not row.is_active:
        return ApiKeyRead.model_validate(row)

    from datetime import UTC, datetime

    row.is_active = False
    row.revoked_at = datetime.now(UTC)
    session.add(row)
    session.commit()
    session.refresh(row)
    return ApiKeyRead.model_validate(row)


@router.delete("/{key_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_api_key(key_id: str, current_user: CurrentUser, session: SessionDep) -> Response:
    row = session.get(ApiKey, key_id)
    if row is None or row.owner_user_id != current_user.id:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="API key not found")

    linked_agents = session.exec(select(Agent).where(Agent.last_api_key_id == key_id)).all()
    for agent in linked_agents:
        agent.last_api_key_id = None
        session.add(agent)

    session.delete(row)
    session.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)
