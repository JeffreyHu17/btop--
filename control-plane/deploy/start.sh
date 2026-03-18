#!/bin/sh
set -eu

cd /workspace/backend
mkdir -p /srv/btop/data

stop_requested=0
uvicorn app.main:app --host 127.0.0.1 --port 8000 &
uvicorn_pid=$!

nginx -g 'daemon off;' &
nginx_pid=$!

cleanup() {
  kill "$uvicorn_pid" "$nginx_pid" 2>/dev/null || true
}

handle_signal() {
  stop_requested=1
  cleanup
}

trap handle_signal INT TERM

while kill -0 "$uvicorn_pid" 2>/dev/null && kill -0 "$nginx_pid" 2>/dev/null; do
  sleep 2
done

cleanup
wait "$uvicorn_pid" 2>/dev/null || true
wait "$nginx_pid" 2>/dev/null || true

if [ "$stop_requested" -eq 1 ]; then
  exit 0
fi

exit 1
