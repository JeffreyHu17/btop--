from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse

from app.bootstrap import ensure_bootstrap_admin
from app.core.config import get_settings
from app.db import create_db_and_tables, engine
from app.routers import agents, api_keys, auth, compat, dashboards, metrics, users

settings = get_settings()


@asynccontextmanager
async def lifespan(app: FastAPI):
    del app
    create_db_and_tables()
    from sqlmodel import Session

    with Session(engine) as session:
        ensure_bootstrap_admin(session)
    yield


app = FastAPI(title=settings.app_name, lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/healthz")
def healthz() -> dict[str, str]:
    return {"status": "ok"}


app.include_router(compat.router)
app.include_router(auth.router, prefix=settings.api_prefix)
app.include_router(users.router, prefix=settings.api_prefix)
app.include_router(api_keys.router, prefix=settings.api_prefix)
app.include_router(agents.router, prefix=settings.api_prefix)
app.include_router(dashboards.router, prefix=settings.api_prefix)
app.include_router(metrics.router, prefix=settings.api_prefix)

frontend_root = settings.frontend_dist_dir
frontend_index = frontend_root / "index.html"


def resolve_frontend_asset(request_path: str) -> FileResponse:
    if not frontend_index.exists():
        raise HTTPException(status_code=404, detail="Frontend build not found")

    candidate = (frontend_root / request_path).resolve()
    if candidate.is_file():
        try:
            candidate.relative_to(frontend_root.resolve())
            return FileResponse(candidate)
        except ValueError:
            pass
    return FileResponse(frontend_index)


@app.get("/", include_in_schema=False)
def serve_frontend_root() -> FileResponse:
    return resolve_frontend_asset("index.html")


@app.get("/{full_path:path}", include_in_schema=False)
def serve_frontend(full_path: str) -> FileResponse:
    if full_path.startswith("api/") or full_path == "healthz":
        raise HTTPException(status_code=404, detail="Not found")
    return resolve_frontend_asset(full_path)
