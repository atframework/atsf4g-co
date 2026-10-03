---
title: 压测机器人（robot）
---

# 压测机器人（robot）

`src/robot/` 是 Go 模拟客户端与压测工具，基于
[robot-go](https://github.com/atframework/robot-go)。

## 快速上手

1. 在已配置的 C++ 工程中选择 robot 构建目标：

```bash
cmake --build <BUILD_DIR> --config Debug --target robot-build
```

   robot 不属于默认 ALL 构建；此目标通过 go-task 生成 Go 协议、RPC handler 并构建客户端。
2. 按[运行与部署](../getting-started/run-deploy)生成 robot 配置并启动所需服务。
   可执行文件在 `<BUILD_DIR>/publish/robot/bin/robot`（Windows 为 `robot.exe`）。
3. 先查看 `robot --help` 和 `src/robot/cmd/` 的命令，再从一次登录与业务请求开始。
   匹配场景参数参照 `src/robot/case_config/`，好友入口见 `cmd/friend.go`。
4. 验证请求/响应后再增加并发与运行时长；检查服务日志及遥测。

## 定制与详细设计

### 结构

```text
src/robot/
├── main.go
├── cmd/                 # CLI 命令
├── rpc/                 # 业务 RPC 封装
├── task/                # 压测任务
├── case_config/         # 场景配置
├── Taskfile.yml         # go-task 构建入口
└── atframework/         # Go 依赖与协议 SDK
```

### 协议生成

robot 使用 atgateway v2 协议完成握手、加密、压缩与重连。
Go protobuf 与 RPC handler 由 `Taskfile.yml`、`generate-for-pb.yaml` 和
`template/robot_rpc_handle.go.mako` 的现有流程生成。
新增 CS RPC 后重新构建 robot，再按业务需要补充命令和任务。

### 部署与遥测

`install/cloud-native/charts/robot/` 提供配置和部署资源，values 设置目标和压测参数。
OpenTelemetry 配置使用 `modules/telemetry.yaml` 和 robot chart 中的现有配置。
