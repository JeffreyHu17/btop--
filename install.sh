#!/usr/bin/env bash
set -euo pipefail

REPO_SLUG="${BTOP_REPO:-JeffreyHu17/btop--}"
SERVICE_NAME="btop-agent"
ACTION="${1:-install}"
VERSION_ARG="${2:-latest}"
ASSET_PATH="${BTOP_AGENT_ASSET_PATH:-}"
CHECKSUMS_PATH="${BTOP_AGENT_CHECKSUMS_PATH:-}"

PLATFORM_OS=""
PLATFORM_ARCH=""
RESOLVED_VERSION=""
INSTALL_ROOT=""
CONFIG_ROOT=""
CONFIG_PATH=""
STATE_PATH=""
BIN_PATH=""
SERVICE_KIND="none"
SERVICE_PATH=""
PLIST_DOMAIN=""
RELEASE_METADATA_FILE=""
DOWNLOADED_ASSET_NAME=""

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

log() {
  printf '%s\n' "$*"
}

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

set_private_file_permissions() {
  chmod 600 "$1"
}

ensure_private_dir() {
  mkdir -p "$1"
  chmod 700 "$1"
}

validate_integer_range() {
  local value="$1"
  local min_value="$2"
  local max_value="$3"
  local field_name="$4"
  [[ "$value" =~ ^[0-9]+$ ]] || die "$field_name must be an integer"
  if (( value < min_value || value > max_value )); then
    die "$field_name must be between $min_value and $max_value"
  fi
}

prompt_validated_integer() {
  local prompt="$1"
  local default_value="$2"
  local min_value="$3"
  local max_value="$4"
  local field_name="$5"
  local result result_num
  while true; do
    result="$(prompt_default "$prompt" "$default_value")"
    if [[ "$result" =~ ^[0-9]+$ ]]; then
      result_num=$((10#$result))
      if (( result_num >= min_value && result_num <= max_value )); then
        printf '%s' "$result_num"
        return 0
      fi
    fi
    printf 'Invalid %s. Expected an integer between %s and %s.\n' "$field_name" "$min_value" "$max_value" >&2
  done
}

write_config_file() {
  local target_path="$1"
  local server_address="$2"
  local server_port="$3"
  local auth_token="$4"
  local collection_interval_ms="$5"
  local enable_gpu="$6"
  BTOP_CONFIG_PATH="$target_path" \
  BTOP_SERVER_ADDRESS="$server_address" \
  BTOP_SERVER_PORT="$server_port" \
  BTOP_AUTH_TOKEN="$auth_token" \
  BTOP_COLLECTION_INTERVAL_MS="$collection_interval_ms" \
  BTOP_ENABLE_GPU="$enable_gpu" \
  python3 - <<'PY'
import json
import os
import pathlib

path = pathlib.Path(os.environ["BTOP_CONFIG_PATH"])
path.write_text(json.dumps(
    {
        "mode": "distributed",
        "run_mode": "interactive",
        "server_address": os.environ["BTOP_SERVER_ADDRESS"],
        "server_port": int(os.environ["BTOP_SERVER_PORT"]),
        "auth_token": os.environ["BTOP_AUTH_TOKEN"],
        "collection_interval_ms": int(os.environ["BTOP_COLLECTION_INTERVAL_MS"]),
        "enable_gpu": os.environ["BTOP_ENABLE_GPU"] == "true",
        "reconnect_delay_ms": 5000,
        "max_reconnect_attempts": 10,
        "log_file": "",
        "pid_file": "",
    },
    indent=2,
) + "\n")
PY
  set_private_file_permissions "$target_path"
}

write_config_interactive() {
  local output_path="${1:-$CONFIG_PATH}"
  local current_host current_port current_interval current_gpu
  current_host="$(read_json_field "$CONFIG_PATH" server_address 2>/dev/null || printf '127.0.0.1')"
  current_port="$(read_json_field "$CONFIG_PATH" server_port 2>/dev/null || printf '9000')"
  current_interval="$(read_json_field "$CONFIG_PATH" collection_interval_ms 2>/dev/null || printf '1000')"
  current_gpu="$(read_json_field "$CONFIG_PATH" enable_gpu 2>/dev/null || printf 'true')"

  local raw_endpoint
  raw_endpoint="$(prompt_default 'Control plane URL or hostname' "$current_host")"
  local endpoint_parts scheme parsed_host parsed_port
  endpoint_parts="$(split_endpoint "$raw_endpoint")"
  scheme="$(printf '%s\n' "$endpoint_parts" | sed -n '1p')"
  parsed_host="$(printf '%s\n' "$endpoint_parts" | sed -n '2p')"
  parsed_port="$(printf '%s\n' "$endpoint_parts" | sed -n '3p')"

  if [[ "$scheme" == "https" ]]; then
    die "https endpoints are not supported by the current agent transport yet"
  fi
  [[ -n "$parsed_host" ]] || die "server host is required"
  current_host="$parsed_host"
  if [[ -n "$parsed_port" ]]; then
    current_port="$parsed_port"
  fi

  current_port="$(prompt_validated_integer 'Control plane port' "$current_port" 1 65535 'control plane port')"
  local current_token
  current_token="$(prompt_secret 'API key')"
  [[ -n "$current_token" ]] || die "API key is required"
  current_interval="$(prompt_validated_integer 'Collection interval in milliseconds' "$current_interval" 250 86400000 'collection interval')"

  if prompt_yes_no 'Enable GPU collection?' "$( [[ "$current_gpu" == 'true' ]] && printf 'Y' || printf 'N' )"; then
    current_gpu='true'
  else
    current_gpu='false'
  fi

  ensure_private_dir "$(dirname "$output_path")"
  write_config_file "$output_path" "$current_host" "$current_port" "$current_token" "$current_interval" "$current_gpu"
}

systemd_escape_arg() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//%/%%}"
  printf '%s' "$value"
}

prompt_default() {
  local prompt="$1"
  local default_value="$2"
  local result
  read -r -p "$prompt [$default_value]: " result
  printf '%s' "${result:-$default_value}"
}

prompt_secret() {
  local prompt="$1"
  local result
  read -r -s -p "$prompt: " result
  printf '\n' >&2
  printf '%s' "$result"
}

prompt_yes_no() {
  local prompt="$1"
  local default_value="$2"
  local result
  read -r -p "$prompt [$default_value]: " result
  result="${result:-$default_value}"
  case "$(printf '%s' "$result" | tr '[:upper:]' '[:lower:]')" in
    y|yes) return 0 ;;
    n|no) return 1 ;;
    *) return 1 ;;
  esac
}

detect_platform() {
  case "$(uname -s)" in
    Linux) PLATFORM_OS="linux" ;;
    Darwin) PLATFORM_OS="macos" ;;
    *) die "unsupported operating system: $(uname -s)" ;;
  esac

  case "$(uname -m)" in
    x86_64|amd64) PLATFORM_ARCH="x86_64" ;;
    arm64|aarch64) PLATFORM_ARCH="arm64" ;;
    i386|i686) PLATFORM_ARCH="x86" ;;
    *) die "unsupported architecture: $(uname -m)" ;;
  esac
}

resolve_roots() {
  if [[ "$PLATFORM_OS" == "linux" ]]; then
    if ! command -v systemctl >/dev/null 2>&1 || [[ ! -d /run/systemd/system ]]; then
      die "Linux installs currently require systemd for persistent service management"
    fi
    if [[ "$(id -u)" -eq 0 ]]; then
      INSTALL_ROOT="/opt/btop-agent"
      CONFIG_ROOT="/etc/btop-agent"
      SERVICE_KIND="systemd-system"
      SERVICE_PATH="/etc/systemd/system/${SERVICE_NAME}.service"
    else
      INSTALL_ROOT="$HOME/.local/share/btop-agent"
      CONFIG_ROOT="$HOME/.config/btop-agent"
      SERVICE_KIND="systemd-user"
      SERVICE_PATH="$HOME/.config/systemd/user/${SERVICE_NAME}.service"
    fi
  else
    if [[ "$(id -u)" -eq 0 ]]; then
      INSTALL_ROOT="/usr/local/lib/btop-agent"
      CONFIG_ROOT="/usr/local/etc/btop-agent"
      SERVICE_KIND="launchd-system"
      SERVICE_PATH="/Library/LaunchDaemons/io.github.jeffreyhu.btop-agent.plist"
      PLIST_DOMAIN="system"
    else
      INSTALL_ROOT="$HOME/.local/share/btop-agent"
      CONFIG_ROOT="$HOME/.config/btop-agent"
      SERVICE_KIND="launchd-user"
      SERVICE_PATH="$HOME/Library/LaunchAgents/io.github.jeffreyhu.btop-agent.plist"
      PLIST_DOMAIN="gui/$(id -u)"
    fi
  fi

  CONFIG_PATH="$CONFIG_ROOT/distributed-client.json"
  STATE_PATH="$CONFIG_ROOT/install-state.json"
  BIN_PATH="$INSTALL_ROOT/btop-agent"
}

read_json_field() {
  local file_path="$1"
  local key="$2"
  [[ -f "$file_path" ]] || return 1
  python3 - "$file_path" "$key" <<'PY'
import json
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
key = sys.argv[2]
try:
    data = json.loads(path.read_text())
except Exception:
    sys.exit(1)
value = data.get(key)
if value is None:
    sys.exit(1)
if isinstance(value, bool):
    print('true' if value else 'false')
else:
    print(value)
PY
}

split_endpoint() {
  local raw="$1"
  python3 - "$raw" <<'PY'
import sys
from urllib.parse import urlparse
raw = sys.argv[1].strip()
parsed = urlparse(raw if '://' in raw else f'//{raw}')
scheme = parsed.scheme or ''
host = parsed.hostname or ''
port = str(parsed.port or '')
if not host and raw and '://' not in raw:
    host = raw
print(scheme)
print(host)
print(port)
PY
}

asset_basename() {
  printf 'btop-agent_%s_%s_%s' "$RESOLVED_VERSION" "$PLATFORM_OS" "$PLATFORM_ARCH"
}

fetch_release_metadata() {
  need_cmd curl
  need_cmd python3

  if [[ -n "$ASSET_PATH" ]]; then
    RESOLVED_VERSION="$VERSION_ARG"
    return
  fi

  RELEASE_METADATA_FILE="$TEMP_DIR/release.json"
  local endpoint
  if [[ "$VERSION_ARG" == "latest" ]]; then
    endpoint="https://api.github.com/repos/${REPO_SLUG}/releases/latest"
  else
    endpoint="https://api.github.com/repos/${REPO_SLUG}/releases/tags/${VERSION_ARG}"
  fi

  curl -fsSL -H "Accept: application/vnd.github+json" "$endpoint" -o "$RELEASE_METADATA_FILE"
  RESOLVED_VERSION="$(python3 - "$RELEASE_METADATA_FILE" <<'PY'
import json
import pathlib
import sys
metadata = json.loads(pathlib.Path(sys.argv[1]).read_text())
print(metadata['tag_name'])
PY
)"
}

resolve_release_asset() {
  local primary_name="$1"
  local secondary_name="${2:-}"
  [[ -n "$RELEASE_METADATA_FILE" ]] || die "release metadata file is not available"
  python3 - "$RELEASE_METADATA_FILE" "$primary_name" "$secondary_name" <<'PY'
import json
import pathlib
import sys
metadata = json.loads(pathlib.Path(sys.argv[1]).read_text())
asset_names = [name for name in sys.argv[2:] if name]
for asset in metadata.get('assets', []):
    if asset.get('name') in asset_names:
        print(asset.get('name', ''))
        print(asset.get('browser_download_url', ''))
        sys.exit(0)
sys.exit(1)
PY
}

resolve_agent_asset() {
  local primary_name secondary_name
  primary_name="$(asset_basename).tar.gz"
  secondary_name="btop-agent-${RESOLVED_VERSION}-${PLATFORM_OS}-${PLATFORM_ARCH}.tar.gz"
  resolve_release_asset "$primary_name" "$secondary_name"
}

download_asset() {
  local archive_path="$TEMP_DIR/agent.tar.gz"
  if [[ -n "$ASSET_PATH" ]]; then
    [[ -f "$ASSET_PATH" ]] || die "local asset does not exist: $ASSET_PATH"
    [[ -r "$ASSET_PATH" ]] || die "local asset is not readable: $ASSET_PATH"
    [[ "$ASSET_PATH" == *.tar.gz ]] || die "local asset must be a .tar.gz archive"
    DOWNLOADED_ASSET_NAME="$(basename "$ASSET_PATH")"
    cp "$ASSET_PATH" "$archive_path"
  else
    local asset_info asset_url
    asset_info="$(resolve_agent_asset)" || die "release asset not found for ${PLATFORM_OS}/${PLATFORM_ARCH}"
    DOWNLOADED_ASSET_NAME="$(printf '%s\n' "$asset_info" | sed -n '1p')"
    asset_url="$(printf '%s\n' "$asset_info" | sed -n '2p')"
    curl -fsSL "$asset_url" -o "$archive_path"
  fi
  printf '%s' "$archive_path"
}

download_checksums_manifest() {
  local checksums_path="$TEMP_DIR/SHA256SUMS.txt"
  if [[ -n "$CHECKSUMS_PATH" ]]; then
    [[ -f "$CHECKSUMS_PATH" ]] || die "checksum manifest does not exist: $CHECKSUMS_PATH"
    [[ -r "$CHECKSUMS_PATH" ]] || die "checksum manifest is not readable: $CHECKSUMS_PATH"
    cp "$CHECKSUMS_PATH" "$checksums_path"
    printf '%s' "$checksums_path"
    return 0
  fi

  if [[ -n "$ASSET_PATH" ]]; then
    local sibling_checksums
    sibling_checksums="$(cd "$(dirname "$ASSET_PATH")" && pwd)/SHA256SUMS.txt"
    [[ -f "$sibling_checksums" ]] || die "local installs require BTOP_AGENT_CHECKSUMS_PATH or a sibling SHA256SUMS.txt"
    cp "$sibling_checksums" "$checksums_path"
    printf '%s' "$checksums_path"
    return 0
  fi

  local checksum_info checksum_url
  checksum_info="$(resolve_release_asset "SHA256SUMS.txt")" || die "release checksum manifest not found"
  checksum_url="$(printf '%s\n' "$checksum_info" | sed -n '2p')"
  curl -fsSL "$checksum_url" -o "$checksums_path"
  printf '%s' "$checksums_path"
}

verify_asset_checksum() {
  local archive_path="$1"
  local checksums_path
  checksums_path="$(download_checksums_manifest)"
  python3 - "$checksums_path" "$DOWNLOADED_ASSET_NAME" "$archive_path" <<'PY'
import hashlib
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
asset_name = sys.argv[2]
asset_path = pathlib.Path(sys.argv[3])
expected = None
for line in manifest_path.read_text().splitlines():
    parts = line.strip().split()
    if len(parts) < 2:
        continue
    candidate_hash = parts[0]
    candidate_name = parts[-1].lstrip("./")
    if candidate_name == asset_name:
        expected = candidate_hash.lower()
        break
if expected is None:
    raise SystemExit(f"checksum for {asset_name} not found in manifest")

sha256 = hashlib.sha256()
with asset_path.open("rb") as handle:
    for chunk in iter(lambda: handle.read(1024 * 1024), b""):
        sha256.update(chunk)
actual = sha256.hexdigest().lower()
if actual != expected:
    raise SystemExit(f"checksum mismatch for {asset_name}")
PY
}

write_state_file() {
  ensure_private_dir "$CONFIG_ROOT"
  BTOP_STATE_PATH="$STATE_PATH" \
  BTOP_STATE_VERSION="$RESOLVED_VERSION" \
  BTOP_STATE_INSTALL_ROOT="$INSTALL_ROOT" \
  BTOP_STATE_CONFIG_PATH="$CONFIG_PATH" \
  BTOP_STATE_SERVICE_KIND="$SERVICE_KIND" \
  BTOP_STATE_SERVICE_PATH="$SERVICE_PATH" \
  python3 - <<'PY'
import json
import os
import pathlib

path = pathlib.Path(os.environ["BTOP_STATE_PATH"])
path.write_text(json.dumps(
    {
        "version": os.environ["BTOP_STATE_VERSION"],
        "install_root": os.environ["BTOP_STATE_INSTALL_ROOT"],
        "config_path": os.environ["BTOP_STATE_CONFIG_PATH"],
        "service_kind": os.environ["BTOP_STATE_SERVICE_KIND"],
        "service_path": os.environ["BTOP_STATE_SERVICE_PATH"],
    },
    indent=2,
) + "\n")
PY
  set_private_file_permissions "$STATE_PATH"
}

service_status() {
  case "$SERVICE_KIND" in
    systemd-system)
      systemctl is-active "$SERVICE_NAME" 2>/dev/null || true
      ;;
    systemd-user)
      systemctl --user is-active "$SERVICE_NAME" 2>/dev/null || true
      ;;
    launchd-system|launchd-user)
      if launchctl print "$PLIST_DOMAIN/io.github.jeffreyhu.btop-agent" >/dev/null 2>&1; then
        printf 'active\n'
      else
        printf 'inactive\n'
      fi
      ;;
    *)
      printf 'unsupported\n'
      ;;
  esac
}

stop_service() {
  case "$SERVICE_KIND" in
    systemd-system)
      systemctl stop "$SERVICE_NAME" >/dev/null 2>&1 || true
      ;;
    systemd-user)
      systemctl --user stop "$SERVICE_NAME" >/dev/null 2>&1 || true
      ;;
    launchd-system|launchd-user)
      launchctl bootout "$PLIST_DOMAIN" "$SERVICE_PATH" >/dev/null 2>&1 || true
      ;;
  esac
}

start_service() {
  case "$SERVICE_KIND" in
    systemd-system)
      systemctl daemon-reload
      systemctl enable --now "$SERVICE_NAME"
      ;;
    systemd-user)
      mkdir -p "$HOME/.config/systemd/user"
      systemctl --user daemon-reload
      systemctl --user enable --now "$SERVICE_NAME"
      ;;
    launchd-system|launchd-user)
      launchctl bootstrap "$PLIST_DOMAIN" "$SERVICE_PATH"
      ;;
  esac
}

write_service_definition() {
  mkdir -p "$(dirname "$SERVICE_PATH")"
  local escaped_bin_path escaped_config_path
  escaped_bin_path="$(systemd_escape_arg "$BIN_PATH")"
  escaped_config_path="$(systemd_escape_arg "$CONFIG_PATH")"
  case "$SERVICE_KIND" in
    systemd-system)
      cat > "$SERVICE_PATH" <<EOF
[Unit]
Description=btop agent
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart="$escaped_bin_path" --config "$escaped_config_path"
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
      ;;
    systemd-user)
      cat > "$SERVICE_PATH" <<EOF
[Unit]
Description=btop agent
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart="$escaped_bin_path" --config "$escaped_config_path"
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF
      ;;
    launchd-system|launchd-user)
      mkdir -p "$CONFIG_ROOT/logs"
      cat > "$SERVICE_PATH" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
  <dict>
    <key>Label</key>
    <string>io.github.jeffreyhu.btop-agent</string>
    <key>ProgramArguments</key>
    <array>
      <string>$BIN_PATH</string>
      <string>--config</string>
      <string>$CONFIG_PATH</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>$CONFIG_ROOT/logs/agent.out.log</string>
    <key>StandardErrorPath</key>
    <string>$CONFIG_ROOT/logs/agent.err.log</string>
  </dict>
</plist>
EOF
      ;;
  esac
  chmod 644 "$SERVICE_PATH"
}

preflight_connectivity() {
  local config_path="${1:-$CONFIG_PATH}"
  local host port token base_url
  host="$(read_json_field "$config_path" server_address)"
  port="$(read_json_field "$config_path" server_port)"
  token="$(read_json_field "$config_path" auth_token)"
  base_url="http://${host}:${port}"

  curl -fsSL "$base_url/api/ping" >/dev/null
  curl -fsSL -H "Authorization: Bearer $token" "$base_url/api/auth/status" >/dev/null
}

validate_agent_once() {
  local binary_path="${1:-$BIN_PATH}"
  local config_path="${2:-$CONFIG_PATH}"
  "$binary_path" --config "$config_path" --once
}

prepare_archive_contents() {
  local archive_path="$1"
  local stage_dir="$TEMP_DIR/stage"
  rm -rf "$stage_dir"
  mkdir -p "$stage_dir"
  tar -xzf "$archive_path" -C "$stage_dir"
  [[ -x "$stage_dir/btop-agent/btop-agent" ]] || die "archive is missing btop-agent executable"
  [[ -f "$stage_dir/btop-agent/distributed-client.example.json" ]] || die "archive is missing distributed-client.example.json"
  "$stage_dir/btop-agent/btop-agent" --help >/dev/null
  printf '%s' "$stage_dir/btop-agent"
}

install_archive_contents() {
  local stage_dir="$1"
  mkdir -p "$INSTALL_ROOT"
  install -m 755 "$stage_dir/btop-agent" "$BIN_PATH"
  install -m 644 "$stage_dir/distributed-client.example.json" "$INSTALL_ROOT/distributed-client.example.json"
}

install_action() {
  detect_platform
  resolve_roots
  fetch_release_metadata
  local archive_path stage_dir
  archive_path="$(download_asset)"
  verify_asset_checksum "$archive_path"
  stage_dir="$(prepare_archive_contents "$archive_path")"
  install_archive_contents "$stage_dir"
  write_config_interactive
  preflight_connectivity
  validate_agent_once
  write_service_definition
  start_service
  write_state_file
  log "Installed $SERVICE_NAME $RESOLVED_VERSION"
  log "Binary: $BIN_PATH"
  log "Config: $CONFIG_PATH"
}

update_action() {
  detect_platform
  resolve_roots
  [[ -f "$BIN_PATH" ]] || die "agent is not installed at $BIN_PATH"
  fetch_release_metadata
  local archive_path stage_dir backup_bin backup_example backup_service backup_state
  archive_path="$(download_asset)"
  verify_asset_checksum "$archive_path"
  stage_dir="$(prepare_archive_contents "$archive_path")"
  preflight_connectivity
  validate_agent_once "$stage_dir/btop-agent"
  backup_bin="$TEMP_DIR/btop-agent.previous"
  backup_example="$TEMP_DIR/distributed-client.example.previous.json"
  backup_service="$TEMP_DIR/service.previous"
  backup_state="$TEMP_DIR/install-state.previous.json"
  cp "$BIN_PATH" "$backup_bin"
  [[ -f "$INSTALL_ROOT/distributed-client.example.json" ]] && cp "$INSTALL_ROOT/distributed-client.example.json" "$backup_example"
  [[ -f "$SERVICE_PATH" ]] && cp "$SERVICE_PATH" "$backup_service"
  [[ -f "$STATE_PATH" ]] && cp "$STATE_PATH" "$backup_state"
  stop_service
  if install_archive_contents "$stage_dir" && write_service_definition && start_service && write_state_file; then
    :
  else
    local rollback_failed=0
    log "Update failed. Attempting rollback."
    stop_service >/dev/null 2>&1 || true
    install -m 755 "$backup_bin" "$BIN_PATH" || rollback_failed=1
    if [[ -f "$backup_example" ]]; then
      install -m 644 "$backup_example" "$INSTALL_ROOT/distributed-client.example.json" || rollback_failed=1
    fi
    if [[ -f "$backup_service" ]]; then
      cp "$backup_service" "$SERVICE_PATH" || rollback_failed=1
    fi
    if [[ -f "$backup_state" ]]; then
      cp "$backup_state" "$STATE_PATH" || rollback_failed=1
      set_private_file_permissions "$STATE_PATH" || rollback_failed=1
    else
      rm -f "$STATE_PATH" || rollback_failed=1
    fi
    start_service >/dev/null 2>&1 || rollback_failed=1
    if (( rollback_failed )); then
      die "update failed and automatic rollback did not complete cleanly"
    fi
    die "update failed and previous version was restored"
  fi
  log "Updated $SERVICE_NAME to $RESOLVED_VERSION"
}

configure_action() {
  detect_platform
  resolve_roots
  [[ -f "$BIN_PATH" ]] || die "agent is not installed at $BIN_PATH"
  local candidate_config backup_config
  candidate_config="$TEMP_DIR/distributed-client.candidate.json"
  backup_config="$TEMP_DIR/distributed-client.previous.json"
  [[ -f "$CONFIG_PATH" ]] && cp "$CONFIG_PATH" "$backup_config"
  write_config_interactive "$candidate_config"
  preflight_connectivity "$candidate_config"
  validate_agent_once "$BIN_PATH" "$candidate_config"
  install -m 600 "$candidate_config" "$CONFIG_PATH"
  stop_service
  write_service_definition
  start_service
  write_state_file
  log "Updated configuration at $CONFIG_PATH"
}

status_action() {
  detect_platform
  resolve_roots
  log "Binary: ${BIN_PATH}"
  log "Config: ${CONFIG_PATH}"
  log "State: ${STATE_PATH}"
  log "Installed: $( [[ -f "$BIN_PATH" ]] && printf 'yes' || printf 'no' )"
  log "Config present: $( [[ -f "$CONFIG_PATH" ]] && printf 'yes' || printf 'no' )"
  log "Service: $(service_status)"
  if [[ -f "$STATE_PATH" ]]; then
    log "Version: $(read_json_field "$STATE_PATH" version 2>/dev/null || printf 'unknown')"
  fi
}

uninstall_action() {
  detect_platform
  resolve_roots
  stop_service
  case "$SERVICE_KIND" in
    systemd-system)
      systemctl disable "$SERVICE_NAME" >/dev/null 2>&1 || true
      ;;
    systemd-user)
      systemctl --user disable "$SERVICE_NAME" >/dev/null 2>&1 || true
      ;;
  esac
  [[ -f "$SERVICE_PATH" ]] && rm -f "$SERVICE_PATH"
  [[ -f "$BIN_PATH" ]] && rm -f "$BIN_PATH"
  [[ -f "$INSTALL_ROOT/distributed-client.example.json" ]] && rm -f "$INSTALL_ROOT/distributed-client.example.json"
  [[ -f "$STATE_PATH" ]] && rm -f "$STATE_PATH"
  rmdir "$INSTALL_ROOT" >/dev/null 2>&1 || true
  if prompt_yes_no 'Remove config file too?' 'N'; then
    rm -f "$CONFIG_PATH"
  fi
  log "Uninstalled $SERVICE_NAME"
}

usage() {
  cat <<'EOF'
Usage: ./install.sh [install|update|configure|status|uninstall] [version]

Examples:
  ./install.sh install latest
  ./install.sh update V1.0.0
  ./install.sh status
  BTOP_AGENT_ASSET_PATH=/tmp/btop-agent_V1.0.0_linux_x86_64.tar.gz ./install.sh install V1.0.0
EOF
}

need_cmd tar
need_cmd install
need_cmd python3
need_cmd curl

case "$ACTION" in
  install) install_action ;;
  update) update_action ;;
  configure) configure_action ;;
  status) status_action ;;
  uninstall) uninstall_action ;;
  help|-h|--help) usage ;;
  *) usage; die "unknown action: $ACTION" ;;
esac
