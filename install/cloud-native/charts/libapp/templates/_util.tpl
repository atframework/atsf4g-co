{{- /*
libapp.util.merge will merge two YAML templates and output the result.
This takes an array of three values:
- the top context
- the template name of the overrides (destination)
- the template name of the base (source)
*/}}
{{- define "libapp.util.merge" -}}
{{- $top := first . -}}
{{- $overrides := fromYaml (include (index . 1) $top) | default (dict ) -}}
{{- $tpl := fromYaml (include (index . 2) $top) | default (dict ) -}}
{{- with (merge $overrides $tpl) -}}
{{- toYaml . -}}
{{- end -}}
{{- end -}}

{{- /*
libapp.util.merge_yaml will merge two YAML values and output the result.
This takes an array of two values:
- the yaml value of the overrides (destination)
- the yaml value of the base (source)
*/}}
{{- define "libapp.util.merge_yaml" -}}
{{- $overrides := fromYaml (first .) | default (dict ) -}}
{{- $tpl := fromYaml (index . 1) | default (dict ) -}}
{{- toYaml (merge $overrides $tpl) -}}
{{- end -}}

{{- define "libapp.vector.server_log_parse" -}}
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
{{- end -}}

{{- define "libapp.vector.server_log_index" -}}
{{- with .Values.vector.index }}
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