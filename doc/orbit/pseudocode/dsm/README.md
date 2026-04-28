# dsm

用于编写 DSM 侧伪代码，负责全局管理平面而不是业务热路径。

当前已经起好的骨架包括：

- `service/app/dsm_main.pseudo.*`：DSM 服务入口与生命周期骨架
- `service/app/handle_ss_rpc_managerservice.pseudo.*`：ManagerService 入站注册与分发入口
- `service/rpc/managerservice.pseudo.*`：ManagerService facade 与 task_action 桥接
- `topology/cluster_catalog.pseudo.*`：DSM 侧最小 controller inventory 聚合落点
- `service/logic/action/task_action_register_controller.pseudo.*`：Phase 5 第一条已落的控制面动作
- `service/logic/action/task_action_report_controller_snapshot.pseudo.*`：把 DSC 周期 snapshot 写进 controller 聚合字段，并同步 DS detail 基线
- `service/logic/action/task_action_query_inventory.pseudo.*`：基于 `cluster_catalog` 的 region / controller 摘要、controller detail 和 DS detail 查询基线

当前建议优先覆盖：

- `service/app`：DSM 服务入口、module_impl、管理面 RPC wiring
- `service/rpc`：DSM facade 与 ControllerAdmin facade
- `service/logic/action`：继续补 `query_inventory` 的 DSA / owner 过滤与更完整 DS 视图、`drain_controller`、`drain_region`、`stop_dedicated_server`
- `topology`：Region / DSC / DSA / DS inventory 聚合
- `control`：维护窗口、路由计划、审计与动作编排

详细职责与时序见 [doc/dsm-guide.md](../../doc/dsm-guide.md)。