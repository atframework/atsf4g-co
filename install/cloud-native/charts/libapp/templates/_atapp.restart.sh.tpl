{{- define "atapp.restart.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
cd "$( dirname "$0" )"

if [ ! -x ./stop.sh_{{ $bus_addr }} ]; then
    chmod +x ./stop.sh_{{ $bus_addr }};
fi
bash ./stop.sh_{{ $bus_addr }} --upgrade

if [ ! -x ./stop.sh_{{ $bus_addr }} ]; then
    chmod +x ./start.sh_{{ $bus_addr }};
fi
bash ./start.sh_{{ $bus_addr }}
{{- end }}