# Emit NUL-separated runtime paths. Do not follow directory links out of the deployment.
service_runtime_paths() {
  local root="$1" path parent
  for path in "$root"/*/bin/*.pid.service "$root/otelcol/bin/otelcol.service" \
      "$root/var/service-manager/supervisor"; do
    if [[ -e "$path" || -L "$path" ]]; then printf '%s\0' "$path"; fi
  done
  # Dependency work directories may have been selected with --work-dir.
  while IFS= read -r -d '' path; do
    parent="$(dirname "$path")"
    [[ "$parent" != "$root/var/service-manager/"* ]] || continue
    if [[ -f "$parent/lifecycle.record" || -f "$parent/lifecycle.record.bak" ]] ||
        [[ -f "$parent/backend" && -f "$parent/key" && -f "$parent/unit" && -f "$parent/kind" ]]; then
      printf '%s\0' "$parent"
      for path in "$(dirname "$parent")"/etcd.pid "$(dirname "$parent")"/redis.cid; do
        if [[ -e "$path" || -L "$path" ]]; then printf '%s\0' "$path"; fi
      done
    fi
  done < <(find "$root" \( -type d \( -name '*.pid.service' -o -name otelcol.service -o -name log \
      -o -path "$root/var/service-manager" \) -prune \) -o \
      \( -type f \( -name backend -o -name lifecycle.record -o -name lifecycle.record.bak \) -print0 \))
  for path in "$root"/*/bin/*.pid "$root"/*/bin/*.pid.old "$root"/*/bin/*.startup-error \
      "$root"/*/bin/*.cid "$root"/*/bin/*.sock "$root"/tools/script/prepare-package.*.lock; do
    if [[ -e "$path" || -L "$path" ]]; then printf '%s\0' "$path"; fi
  done
}
