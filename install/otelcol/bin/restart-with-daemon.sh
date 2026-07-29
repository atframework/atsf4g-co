#!/bin/bash

cd "$(dirname "$0")"

bash ./stop-with-daemon.sh

bash ./start-with-daemon.sh
