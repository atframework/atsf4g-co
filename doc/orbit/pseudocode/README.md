# atorbit 伪代码目录

本目录用于承载 DSA/DSC/DSM 的实现前置产物。

- 设计来源以 `doc/guide.md` 为准。
- 推进顺序以 `doc/implementation-plan.md` 为准。
- 写法与切片规则以 `doc/pseudocode-roadmap.md` 为准。

当前原则：先写伪代码，再进入正式工程代码。

当前已落地：

- Phase 1 的 proto 与 DSA / DSC service 骨架。
- Phase 2 的 DSA 核心模块，包括 `shared/runtime/resource_ledger`、`dsa/process/start_ds_process`、`dsa/agent/local_channel_service`、`dsa/heartbeat/heartbeat_monitor`、`dsa/agent/load_reporter`、`dsa/agent/controller_reporter` 与 `dsa/service/logic/action`。