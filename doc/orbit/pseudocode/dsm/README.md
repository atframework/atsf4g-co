# dsm

当前目录只保留 DSM 占位说明。

原因很简单：`src/component/orbit` 下当前正式协议只覆盖 `common`、`client`、`server`、`agent`、`controller` 五个方向，没有 DSM 的正式 `.proto`、RPC 或字段定义。

因此本目录当前规则是：

- 可以保留伪代码骨架和待确认问题。
- 不把 DSM 管理面、inventory 查询、运维动作编排写成既成事实。
- 不再引用不存在的独立 DSM 设计文档作为当前事实源。

如果后续需要恢复 DSM 设计，应先补正式协议，再回到这里补齐目录说明和伪代码切片。