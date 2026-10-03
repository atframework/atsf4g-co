---
title: 运行与部署
---

# 运行与部署

## 本地快速运行

构建产物在 `<BUILD_DIR>/publish/`。先在
`install/cloud-native/values/personal/` 填写本地覆盖值并构建，或临时修改 publish 中的 values。
至少确认 etcd、Redis 地址、实例布局和 Excel 资源路径。
本地生成脚本依次加载 `default`、`dev`、`personal`，并设置 `global.world_id=1`；
需要其他 world id 时调整调用参数。

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

已有外部 etcd/Redis 时不需要启动临时依赖，但 values 必须使用对应地址。
`generate_config` 调用发布的 atdtool，生成每实例 YAML 和启动脚本，以及 publish 根目录的
`start_all`、`stop_all`、`kill_all`。检查服务日志和 etcd 注册后，再验证业务请求。

## 按实例启动与重载

生成产物按 `<server>/cfg/*_<bus_id>.yaml` 和
`<server>/bin/start_<bus_id>.sh/.ps1` 布局存放。可只启动所需服务实例；
使用数据层的业务服务需配套依赖与组件实例。

热更新使用实例的 `reload_<bus_id>.sh/.ps1`，停止使用 `stop_<bus_id>.sh/.ps1`。
整体停止使用 publish 根目录的 `stop_all.sh/.ps1`；临时依赖由
`tools/script/stop_local_test_env.sh/.ps1` 停止。
修改配置的具体步骤见[服务端配置](../development/server-config)。

## 部署形态

| 形态 | 输入与输出 |
| --- | --- |
| Kubernetes | `install/cloud-native/charts/` 与 values，生成工作负载、HPA、网络和日志资源 |
| Docker | `install/cloud-native/images/server/` 的 Dockerfile 与 entrypoint |
| 裸机/本地 | `non_cloud_native/deploy.yaml` 的进程布局与 sh/ps1 实例脚本 |

日常调整只改 values。新增实例填写 `proc_desc` 的 chart 名、实例数、起始实例号和启动分组。

## 定制与详细设计

需要自行调用渲染工具时，构建后的 atdtool 位于
`<BUILD_DIR>/publish/tools/atdtool/`；Windows 使用 `atdtool.exe`。
传入逗号分隔的 values 目录以沿用本地生成脚本的调用方式：

```bash
atdtool template install/cloud-native/charts -o <OUTPUT_DIR> \
  --values install/cloud-native/values/default,install/cloud-native/values/dev \
  --set global.world_id=1
```

实例配置的生成与部署模式由 chart 控制；原始模板和共享片段位于
`install/cloud-native/charts/libapp/`。
可观测性接入见[遥测](../architecture/telemetry)。
