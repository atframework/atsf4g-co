#!/bin/bash
# Copyright 2026 atframework
# Bash script to download, start, stop, and manage etcd for unit testing.
# Usage: setup-etcd.sh <command> [options]
#   --work-dir DIR        Working directory (default: /tmp/etcd-unit-test)
#   --client-port PORT    Client port (default: 12379)
#   --peer-port PORT      Peer port (default: 12380)
#   --etcd-version VER    Specify version tag (default: latest)

set -e

WORK_DIR="/tmp/etcd-unit-test"
CLIENT_PORT=12379
PEER_PORT=12380
ETCD_VERSION="latest"

# Parse command
COMMAND="${1:-}"
shift || true

# Parse options
while [[ $# -gt 0 ]]; do
  case "$1" in
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --client-port)
      CLIENT_PORT="$2"
      shift 2
      ;;
    --peer-port)
      PEER_PORT="$2"
      shift 2
      ;;
    --etcd-version)
      ETCD_VERSION="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

ETCD_BIN="${WORK_DIR}/etcd"
ETCDCTL_BIN="${WORK_DIR}/etcdctl"
PID_FILE="${WORK_DIR}/etcd.pid"
LOG_FILE="${WORK_DIR}/etcd.log"
DATA_DIR="${WORK_DIR}/data"

get_etcd_version() {
  if [ "$ETCD_VERSION" != "latest" ]; then
    echo "$ETCD_VERSION"
    return
  fi

  echo "Fetching latest etcd version from GitHub..." >&2
  local tag
  tag=$(curl -sSL "https://api.github.com/repos/etcd-io/etcd/releases/latest" | grep '"tag_name"' | sed -E 's/.*"tag_name": *"([^"]+)".*/\1/')
  if [ -z "$tag" ]; then
    echo "Failed to fetch latest etcd version" >&2
    exit 1
  fi
  echo "Latest etcd version: $tag" >&2
  echo "$tag"
}

detect_platform() {
  local os arch
  os=$(uname -s | tr '[:upper:]' '[:lower:]')
  arch=$(uname -m)

  case "$arch" in
    x86_64|amd64) arch="amd64" ;;
    aarch64|arm64) arch="arm64" ;;
    *)
      echo "Unsupported architecture: $arch" >&2
      exit 1
      ;;
  esac

  case "$os" in
    linux) echo "linux-${arch}" ;;
    darwin) echo "darwin-${arch}" ;;
    *)
      echo "Unsupported OS: $os" >&2
      exit 1
      ;;
  esac
}

do_download() {
  local tag platform download_url archive_file

  if [ -f "$ETCD_BIN" ] && [ -f "$ETCDCTL_BIN" ]; then
    echo "etcd binaries already exist at $WORK_DIR, skipping download."
    echo "Use 'cleanup' command first if you want to re-download."
    return 0
  fi

  tag=$(get_etcd_version)
  platform=$(detect_platform)
  mkdir -p "$WORK_DIR"

  local os_name
  os_name=$(uname -s | tr '[:upper:]' '[:lower:]')

  if [ "$os_name" = "darwin" ]; then
    download_url="https://github.com/etcd-io/etcd/releases/download/${tag}/etcd-${tag}-${platform}.zip"
    archive_file="${WORK_DIR}/etcd.zip"
  else
    download_url="https://github.com/etcd-io/etcd/releases/download/${tag}/etcd-${tag}-${platform}.tar.gz"
    archive_file="${WORK_DIR}/etcd.tar.gz"
  fi

  echo "Downloading etcd ${tag} for ${platform}..."
  echo "URL: $download_url"

  curl -sSL -o "$archive_file" "$download_url"

  echo "Extracting..."
  local extract_dir="${WORK_DIR}/etcd-extract"
  rm -rf "$extract_dir"
  mkdir -p "$extract_dir"

  if [ "$os_name" = "darwin" ]; then
    unzip -q "$archive_file" -d "$extract_dir"
  else
    tar -xzf "$archive_file" -C "$extract_dir"
  fi

  # Find the inner directory
  local inner_dir
  inner_dir=$(find "$extract_dir" -maxdepth 1 -mindepth 1 -type d | head -1)

  cp "${inner_dir}/etcd" "$ETCD_BIN"
  cp "${inner_dir}/etcdctl" "$ETCDCTL_BIN"
  chmod +x "$ETCD_BIN" "$ETCDCTL_BIN"

  # Cleanup temp files
  rm -rf "$extract_dir"
  rm -f "$archive_file"

  echo "etcd downloaded successfully to $WORK_DIR"
}

do_start() {
  if [ ! -f "$ETCD_BIN" ]; then
    echo "etcd binary not found. Downloading first..."
    do_download
  fi

  # Check if already running
  if [ -f "$PID_FILE" ]; then
    local existing_pid
    existing_pid=$(cat "$PID_FILE" 2>/dev/null || true)
    if [ -n "$existing_pid" ] && kill -0 "$existing_pid" 2>/dev/null; then
      echo "etcd is already running (PID: $existing_pid). Stopping first..."
      do_stop
    fi
  fi

  mkdir -p "$DATA_DIR"

  echo "Starting etcd on client port $CLIENT_PORT, peer port $PEER_PORT..."

  "$ETCD_BIN" \
    --data-dir "$DATA_DIR" \
    --listen-client-urls "http://127.0.0.1:${CLIENT_PORT}" \
    --advertise-client-urls "http://127.0.0.1:${CLIENT_PORT}" \
    --listen-peer-urls "http://127.0.0.1:${PEER_PORT}" \
    --initial-advertise-peer-urls "http://127.0.0.1:${PEER_PORT}" \
    --initial-cluster "default=http://127.0.0.1:${PEER_PORT}" \
    --log-outputs "$LOG_FILE" \
    > /dev/null 2>&1 &

  local etcd_pid=$!
  echo "$etcd_pid" > "$PID_FILE"

  echo "etcd started with PID: $etcd_pid"

  # Health check with retries
  local max_retries=30
  local retry_count=0
  local healthy=false

  echo "Waiting for etcd to become healthy..."
  while [ $retry_count -lt $max_retries ]; do
    sleep 1
    retry_count=$((retry_count + 1))

    if "$ETCDCTL_BIN" --endpoints="http://127.0.0.1:${CLIENT_PORT}" endpoint health 2>/dev/null; then
      healthy=true
      break
    fi

    # Check if process is still alive
    if ! kill -0 "$etcd_pid" 2>/dev/null; then
      echo "etcd process died during startup. Check $LOG_FILE for details."
      exit 1
    fi
  done

  if [ "$healthy" = true ]; then
    echo "etcd is healthy and ready on http://127.0.0.1:${CLIENT_PORT}"
  else
    echo "etcd failed to become healthy within ${max_retries}s. Check $LOG_FILE for details."
    do_stop
    exit 1
  fi
}

do_stop() {
  if [ ! -f "$PID_FILE" ]; then
    echo "No PID file found. etcd may not be running."
    return 0
  fi

  local pid
  pid=$(cat "$PID_FILE" 2>/dev/null || true)
  if [ -z "$pid" ]; then
    echo "PID file is empty."
    rm -f "$PID_FILE"
    return 0
  fi

  if ! kill -0 "$pid" 2>/dev/null; then
    echo "etcd process (PID: $pid) is not running."
    rm -f "$PID_FILE"
    return 0
  fi

  echo "Stopping etcd (PID: $pid)..."
  kill "$pid" 2>/dev/null || true

  # Wait for process to exit (up to 5 seconds)
  local waited=0
  while [ $waited -lt 5 ]; do
    sleep 1
    waited=$((waited + 1))
    if ! kill -0 "$pid" 2>/dev/null; then
      break
    fi
  done

  # Force kill if still alive
  if kill -0 "$pid" 2>/dev/null; then
    echo "Force killing etcd (PID: $pid)..."
    kill -9 "$pid" 2>/dev/null || true
  fi

  rm -f "$PID_FILE"
  echo "etcd stopped."
}

do_cleanup() {
  do_stop

  echo "Cleaning up $WORK_DIR..."
  rm -rf "$DATA_DIR"
  rm -f "$ETCD_BIN"
  rm -f "$ETCDCTL_BIN"
  rm -f "$LOG_FILE"

  echo "Cleanup complete."
}

do_status() {
  if [ ! -f "$PID_FILE" ]; then
    echo "etcd is not running (no PID file)."
    return 0
  fi

  local pid
  pid=$(cat "$PID_FILE" 2>/dev/null || true)
  if [ -z "$pid" ]; then
    echo "etcd PID file is empty."
    return 0
  fi

  if ! kill -0 "$pid" 2>/dev/null; then
    echo "etcd is not running (PID: $pid not found)."
    return 0
  fi

  echo "etcd is running (PID: $pid)."

  if [ -f "$ETCDCTL_BIN" ]; then
    "$ETCDCTL_BIN" --endpoints="http://127.0.0.1:${CLIENT_PORT}" endpoint health 2>&1 || true
  fi
}

case "$COMMAND" in
  download) do_download ;;
  start) do_start ;;
  stop) do_stop ;;
  cleanup) do_cleanup ;;
  status) do_status ;;
  *)
    echo "Usage: $0 <download|start|stop|cleanup|status> [options]"
    echo ""
    echo "Options:"
    echo "  --work-dir DIR        Working directory (default: /tmp/etcd-unit-test)"
    echo "  --client-port PORT    Client port (default: 12379)"
    echo "  --peer-port PORT      Peer port (default: 12380)"
    echo "  --etcd-version VER    Specify version tag (default: latest)"
    exit 1
    ;;
esac
