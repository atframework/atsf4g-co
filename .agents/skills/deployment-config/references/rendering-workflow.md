# Rendering and local-run workflow

Read this only when rendering deployment output, debugging values precedence, updating chart dependencies, or running a
generated non-Kubernetes instance.

Use `helm` from `PATH` and `atdtool` from `<PUBLISH_DIR>/tools/atdtool/atdtool` (`.exe` on Windows).

## Prepare dependencies and values

- Update chart dependencies with `<PUBLISH_DIR>/tools/script/update_dependency.sh` on Unix-like systems or
  `update_dependency.bat` on Windows.
- Default values are under `<PUBLISH_DIR>/cloud-native/values/default/`: `global.yaml`, `modules/*.yaml`, and
  `non_cloud_native/deploy.yaml` are common inputs.
- Multiple `--values`/`-p` inputs layer in order; later values win. Preserve explicitly supplied paths and `--set`
  values.

## Render

Prefer `<PUBLISH_DIR>/tools/script/generate_config.sh` or `generate_config.bat`. For focused diagnostics, the equivalent
shape is:

```bash
cd <PUBLISH_DIR>/tools/script
../atdtool/atdtool template ../../cloud-native/charts -o ../../ \
  --values ../../cloud-native/values/default \
  --set global.world_id=1
```

Generated config normally appears under `<PUBLISH_DIR>/<server>/cfg/`; generated lifecycle scripts appear under
`<PUBLISH_DIR>/<server>/bin/`.

To inspect precedence without writing into the repository root:

```bash
atdtool merge-values <PUBLISH_DIR>/cloud-native/charts \
  -o <BUILD_DIR>/_agent_tmp/merged.values.yaml \
  --values <PUBLISH_DIR>/cloud-native/values/default \
  --set global.world_id=1
```

## Local instance

Run only the generated script for the intended instance, such as `<PUBLISH_DIR>/echosvr/bin/start_<bus_id>.sh` or
`.bat`. Starting or stopping a process changes external state; confirm the requested instance and follow the repository's
operational procedure rather than treating rendering permission as deployment permission.
