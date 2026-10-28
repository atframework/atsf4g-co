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
addresses. `generate_config` first refreshes packaged chart dependencies with Helm, then invokes the published atdtool,
renders instance YAML/startup scripts, and
generates `start_all`, `stop_all`, and `kill_all` at the publish root.
Dependency update failures stop generation. After editing libapp source templates, refresh each service's
`charts/libapp-*.tgz`; otherwise rendering still uses the old scripts. Before invoking atdtool directly, run
`tools/script/update_dependency.sh/.ps1`.
Check logs and etcd registration before testing business requests.

## Start and Reload Individual Instances

Generated files use `<server>/cfg/*_<bus_id>.yaml` and
`<server>/bin/start_<bus_id>.sh/.ps1`. Start only the instances you need;
business services using the data layer require their dependencies and component instances.

Use the instance's `reload_<bus_id>.sh/.ps1` for reload and `stop_<bus_id>.sh/.ps1` for stopping.
Use the publish root's `stop_all.sh/.ps1` to stop the deployment and
`tools/script/stop_local_test_env.sh/.ps1` to stop temporary dependencies.
See [server configuration](../development/server-config) for configuration changes.

## Process Supervision

Linux shell scripts select a backend through `tools/script/service-manager.sh`. The default `auto` checks for a
reachable systemd user manager, then `supervisord` and `supervisorctl` on PATH, then ordinary background processes.
Having `systemctl` installed without a usable user bus is insufficient. An explicitly requested but unavailable backend fails.

```bash
bash start_all.sh -m auto
bash start_all.sh -m systemd
bash start_all.sh -m supervisord
bash start_all.sh -m none

# Individual instances and dependency services also read this variable.
export ATF_SERVICE_MANAGER=supervisord
bash tools/script/etcd/setup-etcd.sh start --service-manager supervisord
bash tools/script/redis/redis.sh start --service-manager systemd --container-engine podman
```

A running instance retains its original command, arguments, deployment environment variables and backend; stop it before changing them.
Each instance records its controller path, identity and startup arguments under `bin/<PID filename>.service/`.
Etcd/Redis use `service/` in their working directories. Stop, kill and cleanup use this record without probing again
or applying a new backend choice. If the controller is unavailable, state is retained and the operation fails;
restore it and retry. Stop services before moving a deployment directory, and retain state while they run.
Older deployments without a manager record retain their PID-based stop path.

Native processes generate a unit from `tools/script/common/atsf4g@.service` beside `run.sh` in their instance state
directory. `~/.config/systemd/user/` holds only registration and enablement links to that unit; no instance registry
is created in the user's state directory. `%Y` resolves the unit's actual directory in the package.
The instance identifier combines the full service
name with a digest of its state directory, allowing separate deployments to reuse service names. Arguments retain
their original argv boundaries. The server runs in the foreground and systemd tracks the actual process.
Generated scripts use `--atapp-foreground` for their internal foreground mode.
Startup also records the generated stop script for that instance. Normal stop removes startup intent, runs the
application's stop command through that script, and waits for the business process to exit before manager cleanup.
Stop arguments such as `--upgrade` retain their argv boundaries. A failed stop command returns failure and leaves
the process running; retry stop after resolving the cause, or explicitly use kill. Kill bypasses the business stop
command and forcibly terminates the process.
Systemd's `ExecStop` uses the same callback. Supervisor signals a foreground adapter, which calls the stop script
without forwarding that signal to the business process. A successful callback runs once per process lifetime and
is reset after a crash restart. Direct systemctl/supervisorctl stops also invoke it; timeout termination follows
the manager's configuration.
Start uses `systemctl --user enable --now`; stop/kill remove startup intent, disable the instance and stop it through
systemctl. `Restart=on-failure` handles crashes; enable/disable controls startup with the user manager.
After confirming shutdown, stop/kill clear the unit's failed record. If an older test still appears as
`not-found/failed` after its configuration was deleted, run `systemctl --user reset-failed <unit>` for that
confirmed stopped unit. Journal logs remain available.
Running after logout or starting at boot may require user lingering, which operators configure separately.

Supervisor uses a private user daemon, Unix socket and program configurations, by default under
`<package>/var/service-manager/supervisor`. Set `ATF_SUPERVISOR_DIR` before starting to select another absolute directory
without newlines, backslashes or semicolons. The directory must be exclusive to the current user.
Local builds that link `tools` to the source tree still write the default configuration into the published package.
When that path exceeds the Unix socket length limit, only the control socket uses the user runtime directory;
configuration, logs and PID files stay in the package. Stop removes the program
configuration and updates the daemon so restarting the daemon does not revive stopped instances.
Programs use `autorestart=unexpected`; exit code 0 does not trigger restart. Ordinary process mode has no crash
restart. Logs go to the systemd journal, the Supervisor directory, or `output.log` in the instance state directory.

### Existing Container Images

The host supervises an existing image; the application image does not need Supervisor installed. Its main process
must stay in the foreground. An optional second `--` introduces container command arguments; omit it to retain the
image's ENTRYPOINT/CMD.

```bash
export ATF_SERVICE_MANAGER=supervisord
bash tools/script/service-manager.sh start "$PWD/runtime/game-1" game-1 "$PWD" \
  --container docker registry.example/game:version -- \
  --publish 127.0.0.1:7001:7001 --volume "$PWD/config:/app/config:ro"
bash tools/script/service-manager.sh stop "$PWD/runtime/game-1"

# Optional: override the image's CMD.
bash tools/script/service-manager.sh start "$PWD/runtime/redis-1" redis-1 "$PWD" \
  --container podman docker.io/library/redis:7.2.16 -- \
  --publish 127.0.0.1:16379:6379 -- redis-server --appendonly yes
bash tools/script/service-manager.sh kill "$PWD/runtime/redis-1"
```

The controller owns container name, detach, restart, rm, cidfile, sdnotify and other lifecycle options; do not
override them. Docker/Supervisor use a foreground container client. Stop/kill also use the recorded engine to stop
the actual container and verify its exit. Bind mounts, named volumes and images survive stop/kill; do not use the
container's temporary layer or anonymous volumes for persistent data. Redis's explicit cleanup command retains its
existing image-removal behavior.

Podman with systemd uses rootless Quadlet: the per-instance `.container` and enablement drop-in stay in the instance
state directory. `~/.config/containers/systemd/` holds only links to the file and drop-in directory, respecting
`XDG_CONFIG_HOME`, and Podman's generator produces the unit.
Common publish, volume, env and network options become Quadlet fields. Remaining engine options use `PodmanArgs`
and must be compatible with Quadlet's lifecycle settings. This requires a Quadlet-capable Podman and cgroup v2.
On environments such as WSL where the system `network-online.target` is not activated, set
`ATF_QUADLET_WAIT_NETWORK=0` before starting when the image is cached and the service only needs local networking.
The default retains Quadlet's network readiness dependency.
Generated units cannot be enabled/disabled directly with systemctl. Start therefore installs an `[Install]`
drop-in with `WantedBy=default.target`; stop/kill remove it and reload the user manager. Process control still uses
`systemctl --user`. After a successful stop the installed Quadlet link is removed; instance state remains for reuse.

References: [systemd service](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html),
[Supervisor configuration](https://supervisord.org/configuration.html),
[Podman Quadlet](https://docs.podman.io/en/latest/markdown/podman-systemd.unit.5.html).

Older atsf4g images run the server in the background and the configuration watcher in the foreground through
`/entrypoint.sh start`. For these images, mount `tools/script/container-entrypoint.sh` read-only at
`/atf-entrypoint.sh`, set `--entrypoint /bin/bash`, and use `-- /atf-entrypoint.sh start` as the container command,
retaining the original environment and configuration mounts. This compatibility entrypoint reuses the image's
configuration initialization, reload and stop behavior. It calls `health_check` and exits with failure if the server
dies, allowing the host supervisor to restart the container. Startup waits up to 180 seconds by default; adjust with
the container environment variable `ATF_CONTAINER_START_TIMEOUT`. No image rebuild is required. Supervising only the
foreground container client cannot detect a server exit hidden by the old entrypoint.

## Deployment Modes

| Mode | Inputs and outputs |
| --- | --- |
| Kubernetes | `install/cloud-native/charts/` and values generate workloads, networking, storage, and logging configuration |
| Docker | Dockerfile and entrypoint under `install/cloud-native/images/server/` |
| Bare metal/local | Process layout in `non_cloud_native/deploy.yaml` and sh/ps1 instance scripts |

Routine adjustments use values. For new instances, fill `proc_desc` with the chart name, instance count,
starting id, and startup group.
Service-side HPA configuration is generated with each instance. Kubernetes HPA and metrics adapters require
separate configuration; see [HPA integration](../whitepaper/hpa-controller).

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
