#!/bin/bash

cd "$(dirname "$0")"

bash ./stop-with-tcm.sh

bash ./start-with-tcm.sh
