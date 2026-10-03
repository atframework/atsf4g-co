---
title: 新增协议
---

# 新增协议

## 快速上手

1. 选择目录。客户端与服务端共享的消息放在
   `src/server_frame/protocol/public/protocol/pbdesc/`；仅服务端使用的消息放在对应
   `private/protocol/pbdesc/`。共享类型可放 `protocol/common/`；Excel 与运行配置放
   `protocol/config/`。
2. 在已有 proto 中添加 message，或在上述目录新增 proto。沿用相邻文件的 package、import 与
   `go_package`。字段编号不得重复；已发布字段删除时用 `reserved` 保留编号与名称。
3. 在使用它的 proto 中 import 新文件，按[统一构建步骤](overview)重新配置和构建。
   根框架的这些目录已按 `*.proto` 收集，不需要为每个新消息编写生成规则。
4. 若协议位于服务或组件自己的 `protocol/`，将新文件追加到已有
   `project_service_declare_protocol` 或 `project_component_declare_protocol` 的 `PROTOCOLS`
   清单。普通消息到此即可；新 RPC 按[RPC 上手](add-rpc-task)接入。
5. 同步客户端协议并验证请求/响应。增加 proto 消息不会自动产生新的业务行为。

## 验证

确认生成的头文件中存在新类型，依赖它的服务成功编译，客户端和服务端使用相同的字段定义。
不要修改生成的 `.pb.h/.pb.cc` 或 Go `.pb.go`。

## 定制与详细设计

协议发现、sandbox、描述符及生成规则见[RPC 与代码生成](../architecture/rpc-codegen)。
修改 atgateway 握手或线协议时，还需阅读[网关与代理](../architecture/gateway-proxy)。
