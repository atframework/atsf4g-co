#!/bin/bash
#
# pack_publish.sh - Pack the deploy base directory into a zstd-compressed tarball.
#
# Default behavior:
#   * Deploy base directory : <script-dir>/publish
#   * Output directory      : <script-dir>
#   * Output file           : svr-<yyyyMMdd_HHmmss>.tar.zst
#   * Excluded from archive : every directory named "log" (including otelcol/log),
#                             and every "*.a" / "*.dbg" file.
#
# Usage:
#   ./pack_publish.sh
#   ./pack_publish.sh -d <deploy_base_dir> -o <output_dir>
#   ./pack_publish.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DEPLOY_DIR="${SCRIPT_DIR}/publish"
OUTPUT_DIR="${SCRIPT_DIR}"

print_help() {
    cat <<EOF
usage: $(basename "$0") [-d deploy_base_dir] [-o output_dir]
  -d <deploy_base_dir>  Directory to pack. Default: <script-dir>/publish
  -o <output_dir>       Directory for svr-*.tar.zst. Default: <script-dir>
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -d)
            DEPLOY_DIR="$2"
            shift 2
            ;;
        -o)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h | --help)
            print_help
            exit 0
            ;;
        *)
            echo "[ERROR] Unknown option: $1" >&2
            print_help >&2
            exit 1
            ;;
    esac
done

if [ ! -d "${DEPLOY_DIR}" ]; then
    echo "[ERROR] Deploy base directory does not exist: ${DEPLOY_DIR}" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_FILE="${OUTPUT_DIR}/svr-${TIMESTAMP}.tar.zst"

# A matched directory (named "log") is not descended into, so its whole tree is skipped.
EXCLUDES=(
    --exclude='log'
    --exclude='*/log'
    --exclude='log/*'
    --exclude='*/log/*'
    --exclude='*.a'
    --exclude='*.dbg'
)

echo "[INFO] Packing ${DEPLOY_DIR} -> ${OUTPUT_FILE}"
if tar --help 2>/dev/null | grep -q -- '--zstd'; then
    tar --zstd -cf "${OUTPUT_FILE}" "${EXCLUDES[@]}" -C "${DEPLOY_DIR}" .
else
    if ! command -v zstd >/dev/null 2>&1; then
        echo "[ERROR] tar does not support --zstd and 'zstd' is not installed" >&2
        exit 1
    fi
    tar -cf - "${EXCLUDES[@]}" -C "${DEPLOY_DIR}" . | zstd -T0 -o "${OUTPUT_FILE}"
fi
echo "[INFO] Done: ${OUTPUT_FILE}"
