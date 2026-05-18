{{- define "libapp.vector.server_log_normal" -}}
.file_path = string!(.file)
.file_name = basename!(.file_path)
. |= parse_regex!(.file_name, r'^(?P<svrname>[A-Za-z0-9_-]+)_(?P<inst_id>\d+.\d+.\d+.\d+)')
del(.file)
del(.file_path)
del(.file_name)
del(.host)
del(.source_type)
del(.timestamp)
.log_type = "{{ . }}"
. |= parse_regex!(.message, r'^\[(?P<log_ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]\[\s*(?P<log_level>[A-Z]+)\s*\]\((?P<caller>[^():]+:\d+)\)')
parsed_ts = parse_timestamp!(.log_ts, "%Y-%m-%d %H:%M:%S%.f", timezone: "Asia/Shanghai")
.log_ts = format_timestamp!(parsed_ts, format: "%FT%T.%3fZ", timezone: "UTC")
._timestamp = to_unix_timestamp(parsed_ts)
{{- end -}}

{{- define "libapp.vector.server_log_index" -}}
{{- with .index }}
index = [{{range $index, $element := .}}{{if $index}}, {{end}}"{{$element}}"{{end}}]
kv_matches = parse_regex_all!(.message, r'\u001F(?P<key>[^=\u001F\s]+)=(?P<value>[^\u001F]+)\u001F')
if kv_matches != null && length(kv_matches) > 0 {
  for_each(kv_matches) -> |_index, value| {
    if includes(index, value.key) {
      key, _ = "$" + value.key
      . = set!(., [key], value.value)
    }
  }
}
{{- end }}
{{- end -}}

{{- define "libapp.vector.server_log_actor" -}}
.file_path = string!(.file)
. |= parse_regex!(.file_path, r'(?:^|[\\/])lobbysvr[\\/]log[\\/][^\\/]+[\\/](?P<zone_id>\d+)-(?P<user_id>\d+)')
del(.timestamp)
del(.log)
del(.file)
del(.file_path)
del(.host)
del(.source_type)
. |= parse_regex!(.message, r'(?P<log_ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})')
parsed_ts = parse_timestamp!(.log_ts, "%Y-%m-%d %H:%M:%S%.f", timezone: "Asia/Shanghai")
.log_ts = format_timestamp!(parsed_ts, format: "%FT%T.%3fZ", timezone: "UTC")
._timestamp = to_unix_timestamp(parsed_ts)
{{- end -}}

{{- define "libapp.vector.server_log_oss" -}}
del(.host)
del(.source_type)
del(.timestamp)
del(.file)
parse_data = parse_regex!(.message, r'^(?P<log_ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(?P<message>\{.*\})$')
parsed_ts = parse_timestamp!(parse_data.log_ts, "%Y-%m-%d %H:%M:%S", timezone: "Asia/Shanghai")
parse_data.log_ts = format_timestamp!(parsed_ts, format: "%FT%T.000Z", timezone: "UTC")
. = parse_json!(parse_data.message)
.log_ts = parse_data.log_ts
._timestamp = to_unix_timestamp(parsed_ts)
{{- end -}}

{{- define "libapp.vector.server_log_crash" -}}
.file_path = string!(.file)
.file_name = basename!(.file_path)
. |= parse_regex!(.file_name, r'^(?P<svrname>[A-Za-z0-9_-]+)_(?P<inst_id>\d+.\d+.\d+.\d+)')
del(.file)
del(.file_path)
del(.file_name)
del(.host)
del(.source_type)
del(.timestamp)
.log_ts = now()
._timestamp = to_unix_timestamp(.log_ts)
{{- end -}}