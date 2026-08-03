---
title: 运行与部署
---

# 运行与部署

## 本地快速运行

构建完成后，`<BUILD_DIR>/publish/` 下按服务分目录存放可执行文件，并附带部署辅助脚本
（`publish/tools/script/`）。本地生成配置与每实例脚本：

```bash
cd <BUILD_DIR>/publish/tools/script
bash generate_config.sh   # Windows 用 generate_config.bat
```

`generate_config.sh` 内部调用 `atdtool template` 渲染 `cloud-native/charts`（叠加
`values/default,dev,personal` 三个 profile），在 publish 根目录为每个服务实例生成
`<server>/cfg/*_<bus_id>.yaml` 配置与 `<server>/bin/start_<bus_id>.(sh|bat)` 启动脚本。
`tools/script/` 下还有 `start_local_test_env` / `stop_local_test_env`、`update_dependency` 等辅助脚本。

## atdtool 部署（install/ 模板）

正式部署使用 `install/cloud-native/` 下的 Helm chart 风格模板，由 `atdtool`（构建产物
`<PUBLISH_DIR>/tools/atdtool/atdtool`）以 Helm 兼容语义渲染：

```bash
atdtool template install/cloud-native/charts/<server> \
  -o <输出目录> \
  --values install/cloud-native/values/default \
  --set global.world_id=1
```

渲染结果为每个实例生成 `<server>/cfg/*_<bus_id>.yaml` 配置与 `<server>/bin/start_<bus_id>.(sh|bat)`
脚本。模板约定：`.yaml.tpl` → YAML、`.sh.tpl` → shell、`.bat.tpl` → Windows 批处理，裸 `.tpl` 为共享
partial（如 `libapp` chart 的 `_atapp.*.tpl`）。

### 部署形态

| 形态 | 说明 |
| --- | --- |
| Kubernetes | Helm chart（statefulset / hpa / Gateway API httproute / vector 日志），`install/cloud-native/charts/` |
| Docker 镜像 | `install/cloud-native/images/server/`（Dockerfile + entrypoint.sh） |
| 裸机/本地 | `values/default/non_cloud_native/deploy.yaml` 定义进程布局（world_id/zone_id/proc_desc）+ sh/bat 脚本 |

四套 values profile（`default` / `dev` / `enable_direct_connection` / `personal`）可叠加覆盖：

```bash
atdtool template ... --values values/default --values values/dev
```

### values 结构（default profile）

- `global.yaml`：全局参数（world/zone、镜像、资源）。
- `atgateway.yaml` / `atproxy.yaml` / `robot.yaml`：各内置服务参数。
- `modules/`：redis、etcd、hpa、telemetry、vector、profiling、cachesvr_shared、cs_session 等横切配置。

## 可观测性

`install/otelcol/` 提供 OpenTelemetry Collector 启动脚本（daemon / systemd 两种模式）；服务的 trace/metrics
配置由 `svr.telemetry.config.proto` 与 values 的 `modules/telemetry.yaml` 驱动。
