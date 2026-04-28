# dsa

用于编写 DSA 侧伪代码，优先覆盖：

- DS 拉起与注册
- 心跳、退出分类与 OOM 保护
- 资源账本与负载上报

当前已落地目录：

- `service/app`：DSA module_impl 与 AgentService 入站 wiring
- `service/rpc`：AgentService facade
- `service/logic/action`：`task_action_start_ds`、`task_action_stop_ds`、`task_action_query_ds_state`
- `process`：`start_ds_process`
- `agent`：`local_channel_service`、`load_reporter`、`controller_reporter`
- `heartbeat`：`heartbeat_monitor`