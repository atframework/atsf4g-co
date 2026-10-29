# Rendering and local-run workflow

Read this only when rendering deployment output, debugging values precedence, updating chart dependencies, or running a
generated non-Kubernetes instance.

Use `helm` from `PATH` and `atdtool` from `<PUBLISH_DIR>/tools/atdtool/atdtool` (`.exe` on Windows).

## Prepare dependencies and values

- `generate_config.sh/.ps1` refresh chart dependencies before rendering and abort if the update fails.
  Before a direct atdtool invocation, run `<PUBLISH_DIR>/tools/script/update_dependency.sh` or `.ps1` explicitly.
  Service charts consume their packaged `charts/libapp-*.tgz`; newer libapp source files alone do not update that
  archive. Inspect both the source helper and the rendered lifecycle script when validating a deployment fix.
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
- Service suffixes use only the logical deployment root (`tools/script/../..`), never argv, CWD, state paths or
  package versions. Preserve that logical path through generated start/stop/kill scripts; do not resolve it into
  a version directory. Stop the old instance before switching a deployment link. Full service names are unique per
  deployment; existing lifecycles retain their recorded unit until stopped.
- Preserve the state directory until a successful stop. Changing backend or startup arguments requires stopping first.
  If a process has exited after an incomplete stop, start may finish cleanup only after the recorded controller
  confirms that no instance or pending restart remains. Unavailable controllers retain state and fail.
  Keep crash restart disabled before force-killing a managed process; killing only its PID can revive it or orphan a container.
- Generated atapp starts record their matching stop script. Normal stop must execute the application stop command
  and preserve stop arguments, including `--upgrade`; do not replace it with a process signal. The internal
  `--atapp-stop-command` path bypasses manager dispatch to avoid recursive locking. Systemd uses `ExecStop`;
  Supervisor uses a foreground adapter with `stopasgroup=false`. A failed command retains the running process
  and returns failure; explicit kill bypasses the business stop callback. Verify command effects and absence of
  TERM delivery, including direct manager stops and crash-restarted processes.
- After an intentional stop/kill, clear only that unit's failed record and verify `ActiveState=inactive`.
  A deleted unit file may still appear as `not-found/failed`; `is-active` returning nonzero alone does not prove cleanup.
- Run `install/tools/script/tests/test_service_manager.py` with `--work-dir <BUILD_DIR>/_agent_tmp/<test-dir>` for
  real backend acceptance. `test_dependency_services.py` additionally needs `--etcd-bin-dir` and `--container-engine`.
  `test_rendered_lifecycle.py --publish-dir <rendered-dir>` executes a rendered two-instance echosvr layout against a
  foreground CLI fixture. Distinguish fixture/script checks from actual etcd/Redis health and container acceptance.
- For cached-image, loopback-only tests on WSL without `network-online.target`, use `ATF_QUADLET_WAIT_NETWORK=0`.
  Keep the production default network dependency unless the deployment explicitly does not require it.
