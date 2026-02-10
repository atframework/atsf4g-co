# Configuration Expression Expansion

Protobuf fields annotated with `enable_expression: true` in the `atapp_configure_meta` extension
(defined in `atapp_conf.proto`) support **environment-variable expression expansion** at config-load time.
This is implemented in **libatapp** and available to all services that load configuration through the
standard `atapp` config loaders (YAML, INI `.conf`, environment-variable files).

## Quick Reference

| Syntax | Description |
| --- | --- |
| `$VAR` | Bare variable — POSIX names only (`[A-Za-z_][A-Za-z0-9_]*`) |
| `${VAR}` | Braced variable — any characters including `.`, `-`, `/` (k8s labels) |
| `${VAR:-default}` | If `VAR` is unset or empty, expand to `default` |
| `${VAR:+word}` | If `VAR` is set and non-empty, expand to `word`; otherwise empty string |
| `\$` | Literal dollar sign (escape) |
| Nested | `${OUTER_${INNER}}`, `${VAR:-${OTHER:-fallback}}` |

## Enabling Expression Expansion

Add the extension annotation to your protobuf field:

```protobuf
import "atframe/atapp_conf.proto";

message my_config {
  string endpoint = 1
      [(atframework.atapp.protocol.CONFIGURE) = { enable_expression: true }];

  map<string, string> label = 2
      [(atframework.atapp.protocol.CONFIGURE) = { enable_expression: true }];
}
```

For `map` fields, **both key and value** are expanded.

## Example: Deployment YAML

```yaml
atapp:
  metadata:
    label:
      app.kubernetes.io/name: "${APP_NAME:-my-service}"
      service_subset: "${SUBSET:-${ZONE:-default}}"
  bus:
    listen:
      - "ipv4://0.0.0.0:${LISTEN_PORT:-12345}"
  log:
    level: "${LOG_LEVEL:-info}"
```

## Example: INI `.conf`

```ini
[atapp.metadata.label]
app.kubernetes.io/name=${APP_NAME:-my-service}

[atapp.bus]
listen=ipv4://0.0.0.0:${LISTEN_PORT:-12345}
```

## Detailed Documentation

For full syntax rules, C++ API, and integration details, see the libatapp skill:

- `atframework/libatapp/.github/skills/configure-expression.md`
