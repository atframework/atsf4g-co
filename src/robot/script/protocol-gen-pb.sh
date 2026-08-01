#!/bin/sh
# POSIX counterpart of protocol-gen-pb.ps1: expand required proto groups and build the
# descriptor pb. Multiple values arrive as one ';'-separated string; relative path forms
# are preserved so generated descriptors stay identical across platforms.
set -e

PROTOC_BIN="$1"
OUTPUT_FILE="$2"
PROTO_PATHS="$3"
PROTO_DIRS="$4"
PROTO_FILES="$5"
INCLUDE_IMPORTS="$6"

if [ -z "$PROTOC_BIN" ] || [ -z "$OUTPUT_FILE" ] || [ -z "$PROTO_PATHS" ] || [ -z "$PROTO_DIRS" ]; then
  echo "Usage: protocol-gen-pb.sh <protoc-bin> <output.pb> <proto-paths> <proto-dirs> [proto-files] [include_imports]" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT_FILE")"

set --
if [ "$INCLUDE_IMPORTS" = "true" ]; then
  set -- "$@" --include_imports
fi

OLD_IFS=$IFS
IFS=';'
for path in $PROTO_PATHS; do
  [ -n "$path" ] && set -- "$@" --proto_path="$path"
done

for dir in $PROTO_DIRS; do
  [ -z "$dir" ] && continue
  found=0
  for f in "$dir"/*.proto; do
    if [ ! -e "$f" ]; then
      echo "No .proto files found in required group: $dir" >&2
      exit 1
    fi
    set -- "$@" "$f"
    found=1
  done
done

if [ -n "$PROTO_FILES" ]; then
  for f in $PROTO_FILES; do
    [ -n "$f" ] && set -- "$@" "$f"
  done
fi
IFS=$OLD_IFS

"$PROTOC_BIN" "$@" -o "$OUTPUT_FILE"
