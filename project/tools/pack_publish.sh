#!/bin/bash
#
# pack_publish.sh - Pack the deploy base directory into a zstd-compressed tarball.
#
# Default behavior:
#   * Deploy base directory : <script-dir>/publish
#   * Output directory      : <script-dir>
#   * Output file           : svr-<yyyyMMdd_HHmmss>.tar.zst
#   * Before packing, split debug symbols: every shared library (.so) under
#     <deploy>/lib that still carries debug sections has its debug info separated
#     into a sibling ".dbg" file with objcopy (same steps as the
#     project_tool_split_target_debug_sybmol CMake helper) and is stripped, so the
#     archive does not carry debug info.
#   * Excluded from archive : every directory named "log" (including otelcol/log),
#                             <deploy>/tools/go-task and <deploy>/tools/helm,
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

# Locate objcopy: GNU objcopy first, then llvm-objcopy (same probing order as the
# project_tool_split_target_debug_sybmol CMake helper).
SPLIT_DEBUG_OBJCOPY=""
if command -v objcopy >/dev/null 2>&1; then
    SPLIT_DEBUG_OBJCOPY="$(command -v objcopy)"
elif command -v llvm-objcopy >/dev/null 2>&1; then
    SPLIT_DEBUG_OBJCOPY="$(command -v llvm-objcopy)"
fi
if command -v readelf >/dev/null 2>&1; then
    SPLIT_DEBUG_READELF="$(command -v readelf)"
else
    SPLIT_DEBUG_READELF=""
fi

# Split debug symbols of every .so under <deploy_base>/lib that still has debug
# sections. Mirrors project_tool_split_target_debug_sybmol: keep debug into
# "<file>.dbg", strip debug/unneeded symbols from the library, then record the
# debug link so debuggers can still find the symbols. Files that already have a
# sibling "<file>.dbg" or carry no debug section are left untouched, so repeated
# packing is a no-op.
split_lib_debug_symbols() {
    local deploy_base="$1"
    local lib_dir="${deploy_base}/lib"
    local so_file=""
    local so_name=""
    local split_count=0

    if [ -z "${SPLIT_DEBUG_OBJCOPY}" ] || [ -z "${SPLIT_DEBUG_READELF}" ]; then
        echo "[WARN] objcopy or readelf not found, skip splitting debug symbols of shared libraries" >&2
        return 0
    fi
    if [ ! -d "${lib_dir}" ]; then
        return 0
    fi

    while IFS= read -r -d '' so_file; do
        # Already split by an earlier build/pack step.
        [ -f "${so_file}.dbg" ] && continue
        # No debug section left (already stripped or a release build).
        if ! "${SPLIT_DEBUG_READELF}" -S "${so_file}" 2>/dev/null | grep -qE '\.debug_'; then
            continue
        fi
        echo "[INFO] Split debug symbols: ${so_file}"
        (
            cd "$(dirname "${so_file}")"
            so_name="$(basename "${so_file}")"
            "${SPLIT_DEBUG_OBJCOPY}" --only-keep-debug "${so_name}" "${so_name}.dbg" || exit 1
            "${SPLIT_DEBUG_OBJCOPY}" --strip-debug --strip-unneeded "${so_name}" || exit 1
            "${SPLIT_DEBUG_OBJCOPY}" --remove-section .gnu_debuglink "${so_name}" 2>/dev/null || true
            "${SPLIT_DEBUG_OBJCOPY}" --add-gnu-debuglink "${so_name}.dbg" "${so_name}" || exit 1
        ) || exit 1
        split_count=$((split_count + 1))
    done < <(find "${lib_dir}" -type f \( -name '*.so' -o -name '*.so.*' \) ! -name '*.dbg' -print0 2>/dev/null)

    if [ "${split_count}" -gt 0 ]; then
        echo "[INFO] Split debug symbols of ${split_count} shared libraries under ${lib_dir}"
    fi
}

split_lib_debug_symbols "${DEPLOY_DIR}"

# A matched directory (named "log", "tools/go-task" or "tools/helm") is not
# descended into, so its whole tree is skipped.
EXCLUDES=(
    --exclude='log'
    --exclude='*/log'
    --exclude='log/*'
    --exclude='*/log/*'
    --exclude='tools/go-task'
    --exclude='*/tools/go-task'
    --exclude='tools/go-task/*'
    --exclude='*/tools/go-task/*'
    --exclude='tools/helm'
    --exclude='*/tools/helm'
    --exclude='tools/helm/*'
    --exclude='*/tools/helm/*'
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
