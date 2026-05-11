#!/usr/bin/env bash

if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
	exit 0
fi

if ! git submodule update --init; then
	echo "warning: git submodule update --init failed" 1>&2
fi
