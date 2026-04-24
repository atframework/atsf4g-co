{{- $bus_addr := include "libapp.busAddr" . -}}

#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

./robotd -mode solo -config ../cfg/robot_{{ $bus_addr }}.yaml -case_file "$@"