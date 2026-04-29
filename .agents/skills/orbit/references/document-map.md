# Document Map

## 文档角色

- `doc/orbit/guide.md`: orbit 当前设计事实来源，负责组件角色、协议边界、消息方向和约束说明。
- `doc/orbit/pseudocode/README.md`: orbit 伪代码总入口，说明哪些目录已落地、哪些目录仍是占位。
- `doc/orbit/pseudocode/proto/README.md`: 正式协议索引，按五份 `.proto` 总结链路与字段语义。
- `src/component/orbit/**/*.proto`: 最终协议事实源，文档只能归纳这里已定义的内容。

## 目录角色

- `src/component/orbit/protocol/common/protocol/common/`: 共享消息、标识、状态、负载快照等基础定义。
- `src/component/orbit/protocol/client/protocol/pbdesc/`: Client 与 Agent 之间的 RPC 与消息。
- `src/component/orbit/protocol/server/protocol/pbdesc/`: Server 与 Controller 之间的 RPC 与消息。
- `src/component/orbit/agent/protocol/protocol/pbdesc/`: Agent 与 Controller 之间的 RPC 与消息。
- `src/component/orbit/controller/protocol/protocol/pbdesc/`: Controller 到 Agent 的下行 RPC。
- `doc/orbit/pseudocode/shared/`: 共用模型、账本、运行时抽象、日志与指标的代码化伪代码。
- `doc/orbit/pseudocode/dsa/`: DSA 内部流程的代码化伪代码。
- `doc/orbit/pseudocode/dsc/`: DSC 内部流程的代码化伪代码。
- `doc/orbit/pseudocode/sdk/`: SDK 接口与交互语义的代码化伪代码。
- `doc/orbit/pseudocode/dsm/`: DSM 占位与待确认内容，当前不作为正式协议事实来源。

## atframework 依赖映射

- `atframework/cmake-toolset`: 构建链路、toolchain、第三方依赖与代码生成。
- `atframework/atframe_utils`: 通用工具、结果类型、日志辅助、私有测试框架。
- `atframework/libatbus`: 总线承载、消息转发、路由与协议基础。
- `atframework/libatapp`: 服务生命周期、配置、etcd、连接器与模块系统。

## 产物形式

- `README.md` 仅用于索引和导航，不代表实现伪代码本身。
- 实现伪代码统一使用 `.pseudo.h`、`.pseudo.cpp` 与 `_test.pseudo.cpp` 文件，例如 `<module>.pseudo.h`、`<module>.pseudo.cpp`、`<module>_test.pseudo.cpp`。
- `.pseudo.*` 文件内部必须保留类、成员、函数、测试用例等结构，不能退化为纯说明文字。
- 目录层级应尽量贴近未来真实工程结构。

## 放置原则

- 组件内部实现优先落到对应组件目录。
- 跨组件协议说明优先落到 `doc/orbit/pseudocode/proto/`，但字段和 RPC 事实必须回溯到 `src/component/orbit/**/*.proto`。
- 横切能力优先落到 `doc/orbit/pseudocode/shared/`。
- 若无法判断归属，先回到 `doc/orbit/guide.md` 按责任边界再决定。