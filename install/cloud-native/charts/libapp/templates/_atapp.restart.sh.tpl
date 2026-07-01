{{- define "atapp.restart.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
cd "$( dirname "$0" )"

if [ ! -x ./stop_{{ $bus_addr }}.sh ]; then
    chmod +x ./stop_{{ $bus_addr }}.sh;
fi
bash ./stop_{{ $bus_addr }}.sh --upgrade

if [ ! -x ./stop_{{ $bus_addr }}.sh ]; then
    chmod +x ./start_{{ $bus_addr }}.sh;
fi
bash ./start_{{ $bus_addr }}.sh
{{- end }}