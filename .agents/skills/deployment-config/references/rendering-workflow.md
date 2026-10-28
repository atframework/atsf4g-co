# Rendering and local-run workflow

Read this only when rendering deployment output, debugging values precedence, updating chart dependencies, or running a
generated non-Kubernetes instance.

Use `helm` from `PATH` and `atdtool` from `<PUBLISH_DIR>/tools/atdtool/atdtool` (`.exe` on Windows).

## Prepare dependencies and values

- Update chart dependencies with `<PUBLISH_DIR>/tools/script/update_dependency.sh` on Unix-like systems or
  `update_dependency.ps1` on Windows.
- Default values are under `<PUBLISH_DIR>/cloud-native/values/default/`: `global.yaml`, `modules/*.yaml`, and
  `non_cloud_native/deploy.yaml` are common inputs.
- Multiple `--values`/`-p` inputs layer in order; later values win. Preserve explicitly supplied paths and `--set`
  values.

## Render

Prefer `<PUBLISH_DIR>/tools/script/generate_config.sh` or `generate_config.ps1`. For focused diagnostics, the equivalent
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

## Process supervision

- Lifecycle scripts use `install/tools/script/service-manager.sh`. `ATF_SERVICE_MANAGER` (or `start_all.sh -m`)
  selects `auto`, `systemd`, `supervisord`, or `none` at start. Auto requires a reachable systemd user manager before
  selecting systemd. Stop/kill must use the instance's recorded backend, controller and unit; never re-detect them.
- Native systemd units are generated from `common/atsf4g@.service` beside the instance's `run.sh`; `%Y` resolves
  that package directory. Generated scripts use `--atapp-foreground` internally. The user unit directory holds
  registration/enablement links only. Preserve the published root when local `tools` links into source. Supervisor defaults
  to `<PUBLISH_DIR>/var/service-manager/supervisor`. Podman Quadlet files and `[Install]` drop-ins also stay in the
  instance state directory, with only discovery links under `~/.config/containers/systemd`; generated units use
  the `[Install]` drop-in instead of
  `systemctl enable/disable`. Docker and Supervisor use an attached container client and must also stop the container.
  For older images whose entrypoint backgrounds the server, mount `container-entrypoint.sh` so server death also
  terminates the container; an attached client alone only observes the container lifetime.
- Preserve the state directory until a successful stop. Changing backend or startup arguments requires stopping first.
  Keep crash restart disabled before force-killing a managed process; killing only its PID can revive it or orphan a container.
- Run `install/tools/script/tests/test_service_manager.py` with `--work-dir <BUILD_DIR>/_agent_tmp/<test-dir>` for
  real backend acceptance. `test_dependency_services.py` additionally needs `--etcd-bin-dir` and `--container-engine`.
  `test_rendered_lifecycle.py --publish-dir <rendered-dir>` executes a rendered two-instance echosvr layout against a
  foreground CLI fixture. Distinguish fixture/script checks from actual etcd/Redis health and container acceptance.
- For cached-image, loopback-only tests on WSL without `network-online.target`, use `ATF_QUADLET_WAIT_NETWORK=0`.
  Keep the production default network dependency unless the deployment explicitly does not require it.
