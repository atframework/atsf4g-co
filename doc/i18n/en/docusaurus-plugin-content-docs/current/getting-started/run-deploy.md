---
title: Run and Deploy
---

# Run and Deploy

## Quick Local Run

After the build completes, `<BUILD_DIR>/publish/` holds per-service directories with the executables, plus
deployment helper scripts (`publish/tools/script/`). To generate configs and per-instance scripts locally:

```bash
cd <BUILD_DIR>/publish/tools/script
bash generate_config.sh   # on Windows use generate_config.ps1
```

`generate_config.sh` internally runs `atdtool template` over `cloud-native/charts` (layering the
`values/default,dev,personal` profiles), producing `<server>/cfg/*_<bus_id>.yaml` configs and
`<server>/bin/start_<bus_id>.(sh|ps1)` startup scripts for each instance under the publish root.
`tools/script/` also ships `start_local_test_env` / `stop_local_test_env`, `update_dependency`, and other helpers.

## atdtool Deployment (install/ templates)

Production deployment uses the Helm chart style templates under `install/cloud-native/`, rendered by `atdtool`
(build artifact `<PUBLISH_DIR>/tools/atdtool/atdtool`) with Helm-compatible semantics:

```bash
atdtool template install/cloud-native/charts/<server> \
  -o <output directory> \
  --values install/cloud-native/values/default \
  --set global.world_id=1
```

The rendered output generates, for each instance, a `<server>/cfg/*_<bus_id>.yaml` config and a
`<server>/bin/start_<bus_id>.(sh|ps1)` script. Template convention: `.yaml.tpl` → YAML, `.sh.tpl` → shell,
`.ps1.tpl` → PowerShell script; bare `.tpl` files are shared partials (such as the `_atapp.*.tpl` files of the
`libapp` chart).

### Deployment Forms

| Form | Description |
| --- | --- |
| Kubernetes | Helm chart (statefulset / hpa / Gateway API httproute / vector logging), `install/cloud-native/charts/` |
| Docker image | `install/cloud-native/images/server/` (Dockerfile + entrypoint.sh) |
| Bare metal / local | `values/default/non_cloud_native/deploy.yaml` defines the process layout (world_id/zone_id/proc_desc) + sh/ps1 scripts |

Four values profiles (`default` / `dev` / `enable_direct_connection` / `personal`) can be stacked and overridden:

```bash
atdtool template ... --values values/default --values values/dev
```

### values Structure (default profile)

- `global.yaml`: global parameters (world/zone, image, resources).
- `atgateway.yaml` / `atproxy.yaml` / `robot.yaml`: parameters for each built-in service.
- `modules/`: cross-cutting configuration such as redis, etcd, hpa, telemetry, vector, profiling,
  cachesvr_shared, cs_session.

## Observability

`install/otelcol/` provides OpenTelemetry Collector startup scripts (daemon and systemd modes); service
trace/metrics configuration is driven by `svr.telemetry.config.proto` and the values file
`modules/telemetry.yaml`.
