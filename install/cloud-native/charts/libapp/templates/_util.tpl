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
