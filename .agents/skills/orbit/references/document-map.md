# Document Map

## 文档角色

- `doc/orbitguide.md`: 设计事实来源，负责架构、协议、状态机和关键约束。
- `doc/orbitimplementation-plan.md`: 实现阶段与推进顺序。
- `doc/orbitpseudocode-roadmap.md`: 伪代码写法、目录规范和开发流程。
- `references/atframework-baseline.md`: atframework submodule 复用顺序与 x-server 风格基线。

## 目录角色

- `proto/`: 正式 Protobuf 协议定义，不使用伪代码替代。
- `doc/orbit/pseudocode/shared/`: 共用模型、账本、运行时抽象、日志与指标的代码化伪代码。
- `doc/orbit/pseudocode/dsa/`: DSA 内部流程的代码化伪代码。
- `doc/orbit/pseudocode/dsc/`: DSC 内部流程的代码化伪代码。
- `doc/orbit/pseudocode/sdk/`: SDK 接口与交互语义的代码化伪代码。
- `doc/orbit/pseudocode/dsm/`: DSM 占位与待确认内容。

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
- 跨组件协议优先落到 `doc/orbit/pseudocode/proto/`。
- 横切能力优先落到 `doc/orbit/pseudocode/shared/`。
- 若无法判断归属，先回到 `doc/orbitguide.md` 按责任边界再决定。