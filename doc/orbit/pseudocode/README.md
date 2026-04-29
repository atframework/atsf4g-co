# atorbit 伪代码目录

本目录用于承载 orbit 协议说明和实现前置伪代码。

当前约束：

- 设计来源以 `doc/orbit/guide.md` 为准。
- 正式协议事实以 `src/component/orbit/**/*.proto` 为准。
- 协议索引与逐文件摘要以 `doc/orbit/pseudocode/proto/README.md` 为准。
- 如果伪代码、说明文和正式 `.proto` 冲突，优先修正文档，不自行扩写协议。

当前目录职责：

- `proto/`：围绕正式 `.proto` 的导航和语义摘要。
- `dsa/`：Agent 侧执行、进程托管、负载与心跳相关伪代码。
- `dsc/`：Controller 侧调度、路由和转发相关伪代码。
- `sdk/`：Server/Client 两侧接入语义与运行时循环伪代码。
- `shared/`：共用模型、运行时抽象、日志和资源账本。
- `dsm/`：占位目录；当前正式协议未覆盖 DSM 管理面，不在本轮文档中展开。

建议使用顺序：

1. 先读 `doc/orbit/guide.md` 明确角色边界。
2. 再读 `proto/README.md` 对照五份正式协议。
3. 最后进入具体伪代码目录查看实现切片。