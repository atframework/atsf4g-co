# Deployment configuration (atsf4g-co)

This repo now uses `atdtool` to render **deployment configs + per-instance scripts** from the charts under `cloud-native/charts`.

All paths below assume you are working inside the build output: `<BUILD_DIR>/publish` (referred to as `<PUBLISH_DIR>`).

## Prerequisites

- `helm` in `PATH` (used to vendor chart dependencies)
- `atdtool` from the build output: `<PUBLISH_DIR>/tools/atdtool/atdtool` (or `atdtool.exe` on Windows)

## 1) Update chart dependencies (once, or when charts change)

Run the wrapper script (recommended):

- Linux/macOS: `bash <PUBLISH_DIR>/tools/script/update_dependency.sh`
- Windows: `<PUBLISH_DIR>\\tools\\script\\update_dependency.bat`

This runs `helm dependency update` for each server chart (to make `libapp` templates available).

## 2) Edit values (deployment “inputs”)

Default values live under:

- `<PUBLISH_DIR>/cloud-native/values/default/`

Common files to edit:

- `global.yaml` (global knobs, endpoints, feature toggles)
- `modules/*.yaml` (module-specific settings like redis)
- `non_cloud_native/deploy.yaml` (process layout for local/non-K8s deploy: `world_id`, `zone_id`, `proc_desc`, etc.)

Tip: you can layer overrides by providing multiple `--values/-p` paths to `atdtool` (last one wins).

## 3) Render configs + scripts (atdtool)

Recommended (wrapper script):

- Linux/macOS: `bash <PUBLISH_DIR>/tools/script/generate_config.sh`
- Windows: `<PUBLISH_DIR>\\tools\\script\\generate_config.bat`

Manual equivalent (adjust values/profile as needed):

```bash
cd <PUBLISH_DIR>/tools/script
../atdtool/atdtool template ../../cloud-native/charts -o ../../ \
  --values ../../cloud-native/values/default \
  --set global.world_id=1
```

Output layout (generated under `<PUBLISH_DIR>`):

- `<PUBLISH_DIR>/<server>/cfg/*_<bus_id>.yaml` (rendered config)
- `<PUBLISH_DIR>/<server>/bin/start_<bus_id>.(sh|bat)` / `stop_...` / `restart_...` / `reload_...`

## 4) Start/stop instances (non-K8s/local)

Example:

- `cd <PUBLISH_DIR>/echosvr/bin`
- `./start_1.1.10.1.sh` (or `start_1.1.10.1.bat` on Windows)

## Optional: inspect merged values

Generate a single merged values file (useful for debugging precedence):

```bash
atdtool merge-values <PUBLISH_DIR>/cloud-native/charts -o merged.values.yaml \
  --values <PUBLISH_DIR>/cloud-native/values/default \
  --set global.world_id=1
```
