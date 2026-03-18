# Control Plane Deployment

## Docker Compose

Build the production image:

```bash
cd /Volumes/Code/btop/control-plane
docker compose build app
```

Start the control-plane service in the background:

```bash
cd /Volumes/Code/btop/control-plane
docker compose up -d app
```

Stop it again:

```bash
cd /Volumes/Code/btop/control-plane
docker compose down
```

View logs:

```bash
cd /Volumes/Code/btop/control-plane
docker compose logs -f app
```

The default runtime endpoint is `http://127.0.0.1:9000`.

## Development Compose Profile

Start backend and frontend dev services together:

```bash
cd /Volumes/Code/btop/control-plane
docker compose --profile dev up --build
```

Frontend dev server: `http://127.0.0.1:3001`

Backend API: `http://127.0.0.1:9000`
