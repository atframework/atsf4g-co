#!/usr/bin/env bash

if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
	exit 0
fi

# post-checkout receives a third argument: 1 for branch/commit checkout,
# 0 for file checkout. File-only checkout does not need submodule updates.
if [[ "${3:-1}" == "0" ]]; then
	exit 0
fi

if ! git submodule update --init; then
	echo "warning: git submodule update --init failed" 1>&2
fi

