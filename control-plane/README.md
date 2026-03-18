# Control Plane

This directory contains the web product:

- `backend/`: FastAPI + SQLite
- `frontend/`: Vite + React
- `deploy/`: runtime scripts and Nginx config
- `docs/`: deployment and migration notes

## Local commands

Backend:

```bash
cd /Volumes/Code/btop/control-plane/backend
conda run -n base uvicorn app.main:app --host 0.0.0.0 --port 9000
```

Frontend:

```bash
cd /Volumes/Code/btop/control-plane/frontend
npm run dev
```

## Docker Compose

Build the production image:

```bash
cd /Volumes/Code/btop/control-plane
docker compose build app
```

Start the control plane:

```bash
cd /Volumes/Code/btop/control-plane
docker compose up -d app
```

Start the dev profile:

```bash
cd /Volumes/Code/btop/control-plane
docker compose --profile dev up --build
```

Docs:

- [DEPLOYMENT.md](/Volumes/Code/btop/control-plane/docs/DEPLOYMENT.md)
