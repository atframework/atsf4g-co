
# 本地环境操作指南

以下命令在构建产物 `<BUILD_DIR>/publish/` 下执行。先配置
`install/cloud-native/values/personal/` 并构建，或临时调整 publish 中的 values；
确认 etcd、Redis 地址与实例布局。完整说明见[运行与部署](../doc/docs/getting-started/run-deploy.md)。

## 启动

本地依赖脚本启动 etcd 和 Redis。使用外部依赖时可跳过该脚本，并在 values 中填写对应地址。
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
