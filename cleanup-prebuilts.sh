#!/bin/bash

cd "$(dirname "$0")"

rm -rf third_party/install
rm -rf third_party/packages

for CACHE_DIR in $(find . -maxdepth 1 -name "build_*" -type d); do
  rm -rf "$CACHE_DIR"
done

for CACHE_DIR in $(find . -name ".mako_modules*"); do
  rm -rf "$CACHE_DIR"
done

for CACHE_DIR in $(find . -name "__pycache__"); do
  rm -rf "$CACHE_DIR"
done

git submodule foreach --recursive git clean -dfx
