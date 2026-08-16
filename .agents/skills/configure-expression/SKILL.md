---
name: configure-expression
description: "Use when: editing or debugging enable_expression annotations and ${VAR} expansion in annotated atapp config fields. Do not use for unrelated YAML, Helm values, or templates."
---

# Configuration Expression Expansion

Only protobuf fields annotated with `enable_expression: true` in the `atapp_configure_meta` extension participate in
environment-expression expansion. Before editing a value, find its protobuf field and confirm the annotation; `${...}`
text alone is not enough to activate this behavior.

## Quick Reference

| Syntax            | Description                                                             |
| ----------------- | ----------------------------------------------------------------------- |
| `$VAR`            | Bare variable — POSIX names only (`[A-Za-z_][A-Za-z0-9_]*`)             |
| `${VAR}`          | Braced variable — any characters including `.`, `-`, `/` (k8s labels)   |
| `${VAR:-default}` | If `VAR` is unset or empty, expand to `default`                         |
| `${VAR:+word}`    | If `VAR` is set and non-empty, expand to `word`; otherwise empty string |
| `\$`              | Literal dollar sign (escape)                                            |
| Nested            | `${OUTER_${INNER}}`, `${VAR:-${OTHER:-fallback}}`                       |

## Add or change an annotation

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

## Verify from source

- Read the nearest `atframework/libatapp/AGENTS.md` before changing libatapp itself.
- `atframework/libatapp/include/atframe/atapp_conf.proto` defines the annotation.
- `atframework/libatapp/src/atframe/atapp_conf.cpp` implements field and map expansion.
- Search current libatapp tests for the exact syntax or edge case before changing semantics; do not treat this compact
  table as a replacement for implementation evidence.
- For deployment-template work, load `deployment-config` separately and validate a representative rendered config.
