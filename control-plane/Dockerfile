# syntax=docker/dockerfile:1.7

FROM node:20-alpine AS frontend-build
WORKDIR /workspace/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

FROM python:3.11-slim AS backend-base
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PIP_NO_CACHE_DIR=1
WORKDIR /tmp
COPY backend/pyproject.toml /tmp/backend-pyproject.toml
RUN python - <<'PY' > /tmp/requirements.txt
import pathlib
import tomllib

project = tomllib.loads(pathlib.Path('/tmp/backend-pyproject.toml').read_text())
for dependency in project['project']['dependencies']:
    print(dependency)
PY
RUN pip install --upgrade pip && pip install -r /tmp/requirements.txt
WORKDIR /workspace/backend
COPY backend/ ./

FROM backend-base AS backend-dev
EXPOSE 9000
CMD ["uvicorn", "app.main:app", "--reload", "--host", "0.0.0.0", "--port", "9000"]

FROM backend-base AS runtime
ENV BTOP_ENV=production \
    BTOP_DATABASE_URL=sqlite:////srv/btop/data/btop-control-plane.db \
    BTOP_CORS_ORIGINS=http://localhost:9000
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl nginx \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /etc/nginx/sites-enabled/default
COPY deploy/nginx.conf /etc/nginx/conf.d/default.conf
COPY deploy/start.sh /usr/local/bin/btop-start
COPY --from=frontend-build /workspace/frontend/dist /srv/frontend
RUN mkdir -p /srv/btop/data /var/cache/nginx /var/lib/nginx /run \
    && chmod +x /usr/local/bin/btop-start
WORKDIR /workspace/backend
EXPOSE 9000
CMD ["/usr/local/bin/btop-start"]
