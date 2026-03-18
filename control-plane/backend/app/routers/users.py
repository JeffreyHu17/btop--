from fastapi import APIRouter, HTTPException, status
from sqlmodel import select

from app.core.security import hash_password
from app.deps import CurrentUser, SessionDep
from app.models import User
from app.schemas import UserCreateRequest, UserRead, UserUpdateRequest

router = APIRouter(prefix="/users", tags=["users"])


def require_superuser(current_user: CurrentUser) -> None:
    if not current_user.is_superuser:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Superuser required")


@router.get("/me", response_model=UserRead)
def read_me(current_user: CurrentUser) -> UserRead:
    return UserRead.model_validate(current_user)


@router.get("", response_model=list[UserRead])
def list_users(current_user: CurrentUser, session: SessionDep) -> list[UserRead]:
    require_superuser(current_user)
    rows = session.exec(select(User).order_by(User.created_at.desc())).all()
    return [UserRead.model_validate(row) for row in rows]


@router.post("", response_model=UserRead, status_code=status.HTTP_201_CREATED)
def create_user(
    payload: UserCreateRequest,
    current_user: CurrentUser,
    session: SessionDep,
) -> UserRead:
    require_superuser(current_user)
    existing = session.exec(select(User).where(User.username == payload.username)).first()
    if existing is not None:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Username already exists")

    user = User(
        username=payload.username,
        password_hash=hash_password(payload.password),
        is_active=payload.is_active,
        is_superuser=payload.is_superuser,
    )
    session.add(user)
    session.commit()
    session.refresh(user)
    return UserRead.model_validate(user)


@router.patch("/{user_id}", response_model=UserRead)
def update_user(
    user_id: str,
    payload: UserUpdateRequest,
    current_user: CurrentUser,
    session: SessionDep,
) -> UserRead:
    require_superuser(current_user)
    user = session.get(User, user_id)
    if user is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="User not found")

    if payload.password is not None:
        user.password_hash = hash_password(payload.password)
    if payload.is_active is not None:
        user.is_active = payload.is_active
    if payload.is_superuser is not None:
        user.is_superuser = payload.is_superuser

    session.add(user)
    session.commit()
    session.refresh(user)
    return UserRead.model_validate(user)
