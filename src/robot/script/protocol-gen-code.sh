#!/bin/sh
# POSIX counterpart of protocol-gen-code.ps1: one protoc invocation per required group
# into a staging root (protoc rewrites outputs unconditionally), then content-stable
# publish of the generated protocol/ subtree so unchanged files keep their timestamps.
set -e

PROTOC_BIN="$1"
PROTO_PATHS="$2"
PROTO_DIRS="$3"
STAGING_DIR="$4"
PUBLISH_ROOT="${5:-.}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -z "$PROTOC_BIN" ] || [ -z "$PROTO_PATHS" ] || [ -z "$PROTO_DIRS" ] || [ -z "$STAGING_DIR" ]; then
  echo "Usage: protocol-gen-code.sh <protoc-bin> <proto-paths> <proto-dirs> <staging-dir> [publish-root]" >&2
  exit 1
fi
if [ -d "$STAGING_DIR" ]; then
  STAGING_RESOLVED=$(cd "$STAGING_DIR" && pwd -P)
  if [ "$STAGING_RESOLVED" = "/" ]; then
    echo "Refusing unsafe staging dir: $STAGING_DIR" >&2
    exit 1
  fi
fi

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"
STAGING_DIR=$(cd "$STAGING_DIR" && pwd -P)
if [ "$STAGING_DIR" = "/" ]; then
  echo "Refusing unsafe staging dir after resolution" >&2
  exit 1
fi

# protoc-gen-go / protoc-gen-go-mutable live in GOPATH/bin
export PATH="$(go env GOPATH)/bin:$PATH"

OLD_IFS=$IFS
IFS=';'
for dir in $PROTO_DIRS; do
  [ -z "$dir" ] && continue
  set --
  for path in $PROTO_PATHS; do
    [ -n "$path" ] && set -- "$@" --proto_path="$path"
  done
  for f in "$dir"/*.proto; do
    if [ ! -e "$f" ]; then
      echo "No .proto files found in required group: $dir" >&2
      exit 1
    fi
    set -- "$@" "$f"
  done
  "$PROTOC_BIN" --go_out="$STAGING_DIR" --mutable_out="$STAGING_DIR" --go_opt=paths=source_relative --mutable_opt=paths=source_relative "$@"
done
IFS=$OLD_IFS

# Scope the mirror to the generated protocol/ subtree only, so sibling files
# (Taskfile.yml etc.) are never touched.
sh "$SCRIPT_DIR/publish-directory.sh" "$STAGING_DIR/protocol" "$PUBLISH_ROOT/protocol"
