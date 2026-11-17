
# 本地环境操作指南

以下命令在构建产物 `<BUILD_DIR>/publish/` 下执行。先配置
`install/cloud-native/values/personal/` 并构建，或临时调整 publish 中的 values；
确认 etcd、Redis 地址与实例布局。完整说明见[运行与部署](../doc/docs/getting-started/run-deploy.md)。

## 启动

本地依赖脚本启动 etcd、Redis 和 OpenObserve。使用外部依赖时可跳过该脚本，并在 values 中填写对应地址。
`generate_config` 加载 `default`、`dev`、`personal`，使用 `global.world_id=1`，
生成实例配置以及 `start_all`、`stop_all`、`kill_all` 脚本。

Windows 使用 PowerShell 7+：

```powershell
Set-Location <BUILD_DIR>/publish
pwsh -NoLogo -NoProfile -File ./tools/script/start_local_test_env.ps1
pwsh -NoLogo -NoProfile -File ./tools/script/generate_config.ps1
pwsh -NoLogo -NoProfile -File ./start_all.ps1
```

Linux/macOS：

```bash
cd <BUILD_DIR>/publish
bash tools/script/start_local_test_env.sh
bash tools/script/generate_config.sh
bash start_all.sh
```

### OpenObserve 本地可观测性

启动脚本通过 Docker、Podman 或 nerdctl 运行 OpenObserve OSS 单节点容器，并自动创建 `atsf4g` 组织。
访问 `http://127.0.0.1:5080`，账号为 `admin@owent.net`。首次启动生成的密码保存在
`var/local-test-env/openobserve/openobserve-root-password.txt`，后续启动复用，不会重置。

- 数据保存在 `var/local-test-env/openobserve/data`，停止脚本不会删除；仅显式执行
  `tools/script/openobserve/openobserve.sh cleanup` 或 `.ps1 -Command cleanup` 才删除数据和凭据。
- 默认保留 14 天，容器限制为 2 CPU、3GB 内存。按每天不超过 5GB 存储估算，70GB 有效数据之外还需为
  WAL、索引、查询缓存和临时文件预留空间，建议至少准备 100GB 可用磁盘。
- `generate_config` 自动加载 `var/local-test-env/openobserve/values`，将同一密码写入 otelcol 的
  Authorization。trace、metrics、logs 和 spanmetrics 分别发送到 `atframework_traces`、
  `atframework_metrics`、`atframework_logs`（spanmetrics 与 metrics 共用）。
- 自定义 OpenObserve 参数可直接调用 `tools/script/openobserve/openobserve.sh/.ps1`。使用外部
  OpenObserve 时不要启动本地实例，并修改 `modules/telemetry.yaml` 中的 exporter 地址与凭据。

客户端连接地址以生成的 `atgateway/cfg/atgateway_*.yaml` 中 `atgateway.listen.address` 为准。
端口由 `atgateway.listen.begin_port` 与实例号计算，不是所有实例都使用 8001。

Orbit 被管理进程的路径与启动参数在 values 的 `orbit-agent.yaml` 中设置：
`orbit_agent.client_path`、`orbit_agent.client_command_line`；seed 模式使用相应的
`seed_client_path`、`seed_client_command_line`。修改后重新生成配置并重启对应 agent。

## 停止

先停止服务，再停止本地依赖。Windows：

```powershell
pwsh -NoLogo -NoProfile -File ./stop_all.ps1
pwsh -NoLogo -NoProfile -File ./tools/script/stop_local_test_env.ps1
```

Linux/macOS：

```bash
bash stop_all.sh
bash tools/script/stop_local_test_env.sh
```

正常停止无法结束进程时，可使用 publish 根目录的 `kill_all.ps1` 或 `kill_all.sh` 强制终止。
