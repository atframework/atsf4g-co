---
name: deployment-config
description: "Use when: editing or rendering install/**/*.tpl Go templates, Helm values, or atdtool-generated deployment configs/scripts. Do not use for runtime deployment operations alone."
---

# Deployment configuration (atsf4g-co)

This repository uses `atdtool` to render deployment configs and per-instance scripts from charts under
`cloud-native/charts`.

Resolve `<BUILD_DIR>` the same way as `../build/SKILL.md`: read `.vscode/settings.json` for `cmake.buildDirectory`;
if absent, infer from clangd `--compile-commands-dir=...` or an existing configured build tree; if no user setting is
readable, use `build`.

Rendered output normally lives under `<BUILD_DIR>/publish` (`<PUBLISH_DIR>`). AI-generated scratch files, rendered
diagnostics, temporary merged values, and script/log output must stay under
`<BUILD_DIR>/_agent_tmp/...`, not the repository root.

## Template source recognition

- Files under `install/**` ending in `.tpl` are Go `text/template` templates rendered by the atdtool/Helm-compatible
  chart flow.
- Parse `{{ ... }}` actions with Go-template semantics: dot context, pipelines, variables, `define`/`template`,
  `if`/`with`/`range`, comments, and whitespace trim markers such as `{{-` and `-}}`.
- The renderer provides project/Helm-style helper functions used by existing templates, such as `include`, `required`,
  `toYaml`, `nindent`, and `dig`; verify function behavior from generator code or nearby templates before changing it.
- When a second suffix exists before `.tpl`, it is the rendered target syntax: `.yaml.tpl` -> YAML, `.sh.tpl` -> POSIX
  shell, `.bat.tpl` -> Windows batch. Analyze both layers: template logic inside `{{ ... }}` and target-language text
  outside actions or in rendered output.
- Bare `.tpl` files such as `_helpers.tpl` and `_util.tpl` are usually helper/partial templates and may not render as
  standalone files; inspect their `define`, `template`, and `include` callers before editing.
- Do not run target-language formatters or linters directly on `*.tpl` sources. Render representative output with
  `atdtool` and the relevant values first, then validate the generated YAML/script.
- If rendered YAML contains `${VAR}` expressions in a protobuf field annotated with `enable_expression`, also load
  `../configure-expression/SKILL.md`. Do not load it for ordinary `${...}` text without confirming that annotation.

## Render and validate

Read [rendering and local-run workflow](references/rendering-workflow.md) only when updating chart dependencies,
choosing values layers, rendering representative output, inspecting merged values, or running a generated local
instance.

After a template edit, render representative values before validating the target YAML, shell, or batch syntax. Review
the rendered diff as well as the template diff; a target-language linter passing on unrendered Go-template source is not
evidence.
