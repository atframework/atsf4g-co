#!/bin/bash

cd "$(dirname "$0")"

if [ ! -e "atframework/cmake-toolset/" ] || [ ! -e "third_party/install" ]; then
  exit 0
fi

CMAKE_TOOLSET_VERSION=$(cd atframework/cmake-toolset/ && git rev-parse --short HEAD)
CMAKE_TOOLSET_VERSION_LENGTH=${#CMAKE_TOOLSET_VERSION}
for PREBUILT_VERSION_FILE in $(find third_party/install -maxdepth 2 -name ".cmake-toolset.version"); do
  PREBUILT_VERSION=$(cat "$PREBUILT_VERSION_FILE")
  if [[ "${PREBUILT_VERSION:0:$CMAKE_TOOLSET_VERSION_LENGTH}" != "$CMAKE_TOOLSET_VERSION" ]]; then
    bash ./cleanup-prebuilts.sh
    break
  fi
done
