---
title: Run and Deploy
---

# Run and Deploy

## Run Locally

Build outputs live in `<BUILD_DIR>/publish/`. Set local overrides in
`install/cloud-native/values/personal/` and build, or temporarily edit published values.
Check etcd/Redis addresses, instance layout, and Excel resource paths.
Local generation loads `default`, `dev`, and `personal` in order and sets `global.world_id=1`;
adjust the invocation for another world id.

### Linux / macOS

```bash
cd <BUILD_DIR>/publish/tools/script
bash start_local_test_env.sh
bash generate_config.sh
cd ../..
bash start_all.sh
```

### Windows

```powershell
Set-Location <BUILD_DIR>/publish/tools/script
pwsh -NoLogo -NoProfile -File start_local_test_env.ps1
pwsh -NoLogo -NoProfile -File generate_config.ps1
Set-Location ../..
pwsh -NoLogo -NoProfile -File start_all.ps1
```

When etcd/Redis are already available externally, omit temporary dependency startup and point values to those
addresses. `generate_config` invokes the published atdtool, renders instance YAML/startup scripts, and
generates `start_all`, `stop_all`, and `kill_all` at the publish root.
Check logs and etcd registration before testing business requests.

## Start and Reload Individual Instances

Generated files use `<server>/cfg/*_<bus_id>.yaml` and
`<server>/bin/start_<bus_id>.sh/.ps1`. Start only the instances you need;
business services using the data layer require their dependencies and component instances.

Use the instance's `reload_<bus_id>.sh/.ps1` for reload and `stop_<bus_id>.sh/.ps1` for stopping.
Use the publish root's `stop_all.sh/.ps1` to stop the deployment and
`tools/script/stop_local_test_env.sh/.ps1` to stop temporary dependencies.
See [server configuration](../development/server-config) for configuration changes.

## Deployment Modes

| Mode | Inputs and outputs |
| --- | --- |
| Kubernetes | `install/cloud-native/charts/` and values generate workloads, HPA, networking, and log resources |
| Docker | Dockerfile and entrypoint under `install/cloud-native/images/server/` |
| Bare metal/local | Process layout in `non_cloud_native/deploy.yaml` and sh/ps1 instance scripts |

Routine adjustments use values. For new instances, fill `proc_desc` with the chart name, instance count,
starting id, and startup group.

## Customization and Design

The built atdtool lives under `<BUILD_DIR>/publish/tools/atdtool/`; use `atdtool.exe` on Windows.
Follow local scripts' comma-separated values directory syntax:

```bash
atdtool template install/cloud-native/charts -o <OUTPUT_DIR> \
  --values install/cloud-native/values/default,install/cloud-native/values/dev \
  --set global.world_id=1
```

Charts control instance generation and deployment modes. Templates and shared fragments live under
`install/cloud-native/charts/libapp/`.
For observability, see [telemetry](../architecture/telemetry).
