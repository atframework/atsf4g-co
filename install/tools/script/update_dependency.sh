#!/bin/bash
cd "$(dirname $0)"

if [[ "x$HELM_BIN" == "x" ]]; then
  HELM_BIN="$(which helm)"
fi

cd ../../cloud-native/charts

servers=$(ls -l | awk '/^d/ {print $NF}')

for svr in ${servers}; do
  if [ $svr = "libapp" -o $svr = "app" ]; then
    continue
  fi
  if [[ ! -e "$svr/Chart.yaml" ]]; then
    continue
  fi
  echo $svr
  "$HELM_BIN" dependency update $svr
done