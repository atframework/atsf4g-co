---
title: 压测机器人（robot）
---

# 压测机器人（robot）

`src/robot/` 是 **Go 语言**实现的压力测试/模拟客户端，基于
[robot-go](https://github.com/atframework/robot-go)（vendored 在 `src/robot/atframework/robot-go/`）。

## 结构

```
src/robot/
├── main.go             # 入口
├── cmd/                # 命令
├── rpc/                # CS RPC 封装
├── task/               # 压测任务
├── db/                 # 数据访问
├── Taskfile            # go-task 构建
└── atframework/        # vendored 依赖（robot-go、atframe-utils-go、atgateway 协议 SDK）
```

## 协议

robot 直接说 atgateway v2 协议（FlatBuffers，`atgateway/libatgw_protocol_sdk.fbs`）：ECDH 握手、加密、
压缩、重连。CS 请求打包 API 由服务端模板 `package_request_api_for_simulator.*.mako` 生成。

## 部署

`install/cloud-native/charts/robot/` 提供 robot 的部署 chart；压测规模、目标 zone 等由 values 配置，
OpenTelemetry 配置见 `e445d276` 之后的 otel 生成优化（`modules/` 配置）。
