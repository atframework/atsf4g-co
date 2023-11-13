#!/bin/bash

cd "$(dirname "$0")"

mkdir -p ../log

./otelcol-contrib --config=../etc/config.yaml
