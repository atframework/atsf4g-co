#!/bin/sh
# Publish files from a staging directory into the target directory, content-stably:
# copy only when content differs, then remove stale files that belong to this flow.
set -e

SRC_INPUT="$1"
DST_INPUT="$2"

if [ -z "$SRC_INPUT" ] || [ -z "$DST_INPUT" ]; then
  echo "SourceDir/TargetDir must not be empty" >&2
  exit 1
fi
if [ ! -d "$SRC_INPUT" ]; then
  echo "SourceDir not found: $SRC_INPUT" >&2
  exit 1
fi

# Resolve before entering either tree. Relative paths must keep the caller's working
# directory as their base, and symlink or lexical aliases of / must be rejected.
SRC=$(cd "$SRC_INPUT" && pwd -P)
mkdir -p "$DST_INPUT"
DST=$(cd "$DST_INPUT" && pwd -P)
if [ "$SRC" = "/" ] || [ "$DST" = "/" ]; then
  echo "Refusing unsafe source or target: $SRC_INPUT -> $DST_INPUT" >&2
  exit 1
fi
if [ "$SRC" = "$DST" ]; then
  echo "SourceDir and TargetDir must differ" >&2
  exit 1
fi

copied=$(
  cd "$SRC"
  find . -type f | while IFS= read -r f; do
    if [ ! -f "$DST/$f" ] || ! cmp -s "$f" "$DST/$f"; then
      mkdir -p "$DST/$(dirname "$f")"
      cp -p "$f" "$DST/$f"
      printf '1\n'
    fi
  done | wc -l | tr -d '[:space:]'
)

removed=$(
  cd "$DST"
  find . -type f | while IFS= read -r f; do
    if [ ! -f "$SRC/$f" ]; then
      rm -f "$f"
      printf '1\n'
    fi
  done | wc -l | tr -d '[:space:]'
)

echo "Published $copied file(s), removed $removed stale file(s) -> $DST"
