# 执行计划 - 单元测试补全

## 通用说明

### 测试框架

使用项目私有测试框架 (frame/test_macros.h)，主要宏:

- `CASE_TEST(group, name)` - 定义测试用例
- `CASE_EXPECT_TRUE/FALSE/EQ/NE/LT/LE/GT/GE` - 断言
- `CASE_MSG_INFO()` - 日志输出
- `CASE_THREAD_SLEEP_MS(ms)` - 线程休眠

### 代码风格

- 命名: snake_case
- 常量: UPPER_SNAKE_CASE
- 静态全局回调函数: `static int/void xxx_callback_fn(...)`
- 静态全局测试数据: `static xxx g_xxx_variable`

### 超时模拟策略

> **背景**: 时间轮定时器 (jiffies_timer) 一个 tick 约 100ms，在虚拟机环境中可能因 CPU 调度抖动或磁盘 IO 争用产生更大延迟。
> 若使用过短的超时配置，测试流程中正常的 `run_noblock()` 循环就可能意外触发超时，导致测试不稳定。

**原则**:

1. **配置文件中的定时器间隔一律放大**，确保在正常测试流程（几秒内的 `run_noblock()` 循环）中
   不会因环境抖动而意外超时。
2. **需要验证超时行为时**，使用 `app::set_sys_now()` 主动推进系统时间来模拟超时，
   而非等待真实时间流逝。
3. `app::set_sys_now()` 仅在 Debug 构建 (`!defined(NDEBUG)`) 可用，通过修改
   `app::get_sys_now()` 的内部偏移量生效。`atapp_connector_atbus` 中所有时间比较
   均使用 `app::get_sys_now()`，因此 `set_sys_now()` 可影响所有超时判断。

**使用模式**:

```cpp
// 1. 正常流程中不需要关心超时 — 配置值足够大，不会意外触发
for (int i = 0; i < 256; ++i) {
  app1.run_noblock();
  app2.run_noblock();
}

// 2. 需要验证超时时 — 主动推进时间
auto now = app::get_sys_now();
// 推进到 lost_topology_timeout 之后
app::set_sys_now(now + std::chrono::seconds(33));
app1.run_noblock();  // 此次 tick 中检测到超时并触发清理
app::set_sys_now(now);  // 测试结束后恢复（或在下个测试前重置）

// 3. 验证重连退避间隔时 — 逐步推进时间
auto base = app::get_sys_now();
// 第1次重连: start_interval = 2s
app::set_sys_now(base + std::chrono::seconds(3));
app1.run_noblock();
// 第2次重连: 2*2 = 4s
app::set_sys_now(base + std::chrono::seconds(8));
app1.run_noblock();
// ...
```

---

## libatbus 单元测试补全 ✅ 已完成

### 项目信息

- **代码目录**: `atframework/libatbus`
  - ⚠️ 忽略 `atframework/libatbus/atframework` 内的所有内容
- **单元测试目录**: `atframework/libatbus/test/case`
- **测试工具头文件**: `atframework/libatbus/test/case/atbus_test_utils.h`

### 参考文件

| 文件 | 用途 |
|------|------|
| `channel_mem_test.cpp` | mem通道测试参考 |
| `channel_io_stream_tcp_test.cpp` | ios通道TCP测试参考 |
| `atbus_node_msg_test.cpp` | node消息测试参考 |
| `atbus_node_reg_test.cpp` | node注册测试参考 |
| `atbus_test_utils.h/cpp` | 测试工具函数 |

---

### A. 通道层事件回调测试 ✅

> ⚠️ **mem通道**: 无回调机制，仅有 `mem_send`/`mem_recv` 同步接口，无需测试回调。

> ⚠️ **ios通道**: 以下回调已在 `channel_io_stream_tcp_test.cpp` 中覆盖，无需新增:
> - `kAccepted` - `accepted_callback_test_fn`
> - `kConnected` - `connected_callback_test_fn`, `listen_callback_test_fn`
> - `kDisconnected` - `disconnected_callback_test_fn` (多处使用: io_stream_tcp_reset_by_client, io_stream_tcp_reset_by_server 等)
> - `kReceived` - `recv_callback_check_fn`

**已新增测试的回调**:

**文件**: `channel_io_stream_tcp_test.cpp` (追加)

| 用例名 | 描述 | 状态 |
|--------|------|------|
| `io_stream_callback_on_written` | 发送完成回调 kWritten | ✅ 通过 |

---

### B. Node层事件回调测试 ✅ (atbus_node.h set_on_XXX_handle)

> ⚠️ 以下回调已在现有测试中覆盖，无需新增:
> - `set_on_forward_request_handle` - atbus_node_msg_test.cpp
> - `set_on_forward_response_handle` - atbus_node_msg_test.cpp
> - `set_on_custom_command_request_handle` - atbus_node_msg_test.cpp
> - `set_on_custom_command_response_handle` - atbus_node_msg_test.cpp
> - `set_on_ping_endpoint_handle` - atbus_node_msg_test.cpp
> - `set_on_pong_endpoint_handle` - atbus_node_msg_test.cpp
> - `set_on_add_endpoint_handle` - atbus_node_reg_test.cpp
> - `set_on_remove_endpoint_handle` - atbus_node_reg_test.cpp, atbus_node_msg_test.cpp
> - `set_on_new_connection_handle` - atbus_node_reg_test.cpp
> - `set_on_invalid_connection_handle` - atbus_node_reg_test.cpp
> - `set_on_register_handle` - atbus_node_reg_test.cpp
> - `set_on_available_handle` - atbus_node_reg_test.cpp
> - `set_on_shutdown_handle` - atbus_node_reg_test.cpp

**已新增测试的回调**:

**文件**: `atbus_node_reg_test.cpp` (追加)

#### B.1 关闭连接回调 ✅

| 回调接口 | 用例名 | 触发场景 | 状态 |
|----------|--------|----------|------|
| `set_on_close_connection_handle` | `on_close_connection_normal` | 连接正常关闭（node1/node2独立计数验证） | ✅ 通过 |
| `set_on_close_connection_handle` | `on_close_connection_by_peer` | 对端关闭连接 | ✅ 通过 |

#### B.2 拓扑相关回调 ✅

| 回调接口 | 用例名 | 触发场景 | 状态 |
|----------|--------|----------|------|
| `set_on_topology_update_upstream_handle` | `on_topology_upstream_set` | 下游节点通过 conf.upstream_address 连接上游时触发 | ✅ 通过 |
| `set_on_topology_update_upstream_handle` | `on_topology_upstream_clear` | 上游节点断开后下游状态变化 | ✅ 通过 |
| `set_on_topology_update_upstream_handle` | `on_topology_upstream_change_id` | 上游地址不变但ID更换（模拟上游重启） | ✅ 通过 |
| `set_on_topology_update_upstream_handle` | `on_topology_upstream_clear` | 清除上游节点 | new_peer为空 |

---

## libatapp 单元测试补全

### 项目信息

- **代码目录**: `atframework/libatapp`
  - ⚠️ 忽略 `atframework/libatapp/atframework` 内的所有内容
- **单元测试目录**: `atframework/libatapp/test/case`

### 参考文件

| 文件 | 用途 |
|------|------|
| `atapp_message_test.cpp` | 消息发送测试参考 |
| `atapp_connector_test.cpp` | 连接器测试参考 |
| `atapp_test_0.yaml` | 单节点配置模板 |
| `atapp_test_1.yaml` | 节点1配置模板 (端口21537) |
| `atapp_test_2.yaml` | 节点2配置模板 (端口21538) |

---

### 待补充单元测试 — 需求总览

> 参考: `atframework/libatapp/test/case/atapp_message_test.cpp` 中的 `CASE_TEST(atapp_message, send_message_remote)`
>
> 关键 YAML 配置字段（位于 `atapp.bus` 下）:
> - `proxy`: 上游节点地址（对应运行时 `upstream_address`）
> - `reconnect_start_interval`: 首次重连间隔（默认 8s，**测试用 2s**）
> - `reconnect_max_interval`: 最大重连间隔（默认 60s，**测试用 16s**）
> - `reconnect_max_try_times`: 最大重试次数（默认 5，**测试用 3**）
> - `lost_topology_timeout`: 丢失拓扑超时（默认 120s，**测试用 32s**）
> - `topology.rule.allow_direct_connection`: 是否允许邻居/远方节点直连
> - `topology.rule.require_same_upstream`: 是否要求同上游才能直连
>
> **注意**: 定时器间隔放大是为了防止虚拟机环境中 CPU/磁盘调度抖动及时间轮 100ms tick 误差
> 导致测试流程中意外触发超时。需要验证超时行为时使用 `app::set_sys_now()` 推进时间。

#### R1. 上游拓扑关系变化，代理节点更新 → 测试 D

| # | 场景 | 对应用例 |
|---|------|----------|
| R1.1 | 新上游已有服务发现数据，新上游未连接，发起连接后正常 | D.1 |
| R1.2 | 新上游已有服务发现数据，新上游已连接，无缝切换 | D.2 |
| R1.3 | 新上游服务发现不存在，稍后获取后连接正常 | D.3 |
| R1.4 | 新上游服务发现数据超时，本节点超时清理 | D.4 |
| R1.5 | 仅丢失拓扑信息，超时前恢复则不影响通信 | D.5 |
| R1.6 | 仅丢失拓扑信息，超时后断开，下游 pending 消息走失败 | D.6 |

#### R2. 连接上游时 → 测试 A

| # | 场景 | 对应用例 |
|---|------|----------|
| R2.1 | 服务发现已存在，直接连接 | A.2 |
| R2.2 | 服务发现不存在，等到达后连接 | A.1 |
| R2.3 | 服务发现不存在，重连超时后关闭并失败 | A.5 |
| R2.4 | 上游断开重连期间，下游消息正常排队不被关闭 | A.4 |
| R2.5 | 上游重连超时后，下游消息失败取消并清理 | A.6 |

#### R3. 直连时 → 测试 B

| # | 场景 | 对应用例 |
|---|------|----------|
| R3.1 | 服务发现已存在，直接连接 | B.1 |
| R3.2 | 服务发现不存在，等到达后连接 | B.2 |
| R3.3 | 服务发现不存在，重连超时后关闭并失败 | B.6 |
| R3.4 | 有上游但允许直连，优先直连而非上游转发 | B.8 |
| R3.5 | 有上游但允许直连，服务发现不存在时等待后连接 | B.9 |

#### R4. 所有关系 → 测试 A/B/C 各含重试成功场景

| # | 场景 | 对应用例 |
|---|------|----------|
| R4.1 | 首次连接失败 → 重试 → 成功 → 发送数据正常 | A.4, B.4, C.3 |

#### R5. 异步流程补充（代码审计发现的边界场景） → 测试 E/F

| # | 场景 | 对应用例 |
|---|------|----------|
| R5.1 | 服务发现删除 → 等待 → 服务发现重新上线 → 恢复连接 | E.1 |
| R5.2 | 服务发现缺失 → 重连计数累积 → max_try_times 超限 → 强制清理 | E.2 |
| R5.3 | 发送失败 → handle 标记 unready → endpoint 移除后重连 → 发送成功 | F.1 |
| R5.4 | 代理节点 ready → 被代理下游节点也 ready；代理 unready → 下游也 unready | F.2 |
| R5.5 | 代理节点被移除 → 下游 handle 也关闭 | F.3 |
| R5.6 | 重连指数退避：间隔从 start_interval 翻倍至 max_interval | E.3 |

---

### A. 通过上游节点转发数据测试

**新建文件**: `atapp_upstream_forward_test.cpp`

> 拓扑结构: `node1(0x101) --proxy--> upstream(0x102) <--proxy-- node3(0x103)`
> node1 发消息给 node3，经 upstream 转发

| # | 用例名 | 描述 | 验证点 | 需求 |
|----|--------|------|--------|------|
| A.1 | `upstream_wait_discovery_then_send` | node3 服务发现延迟注入，node1 先发消息排队，注入后送达 | pending 消息到达 | R2.2 |
| A.2 | `upstream_connected_forward_success` | 上游+目标均已连接，直接转发成功 | 数据正确到达 | R2.1 |
| A.3 | `upstream_connected_target_unreachable` | 上游已连接，目标节点不存在 | 发送失败回调 | - |
| A.4 | `upstream_reconnect_then_send_success` | 上游断开 → 重连成功 → pending 消息送达 | 重连期间消息排队，恢复后到达 | R2.4, R4.1 |
| A.5 | `upstream_retry_exceed_limit_fail` | 上游断开 → 用 `set_sys_now()` 推进时间触发重试超限(reconnect_max_try_times) → handle 移除 | pending 消息失败回调 | R2.3 |
| A.6 | `upstream_retry_timeout_downstream_cleanup` | 上游重连超时（`set_sys_now()` 推进） → 下游 pending 消息失败 → 下游 handle 清理 | 下游节点的 endpoint 也被关闭 | R2.5 |
| A.7 | `upstream_topology_offline_pending_fail` | 上游拓扑移除(remove_topology_peer) → `set_sys_now()` 推进到 lost_topology_timeout 之后 → 强制移除 | pending 消息失败 | - |
| A.8 | `upstream_topology_change_new_upstream` | update_topology_peer 切换上游 → 经新上游转发 | 数据通过新上游到达 | R1.2 |

---

### B. 直连节点数据发送测试

**新建文件**: `atapp_direct_connect_test.cpp`

> 拓扑结构: `node1(0x201) --proxy--> upstream(0x203) <--proxy-- node2(0x202)`
> `allow_direct_connection: true`，node1 和 node2 为 kSameUpstreamPeer 关系

| # | 用例名 | 描述 | 验证点 | 需求 |
|----|--------|------|--------|------|
| B.1 | `direct_discovery_ready_connect_send` | 服务发现已存在，发起直连后发送成功 | 数据正确到达 | R3.1 |
| B.2 | `direct_discovery_missing_wait_then_send` | 服务发现延迟注入，等待后发起连接发送 | pending 消息到达 | R3.2 |
| B.3 | `direct_connected_send_success` | 已直连，直接发送成功 | 数据正确到达 | - |
| B.4 | `direct_reconnect_success_after_failure` | 直连断开 → 重连成功 → 发送成功 | 验证有过失败、验证恢复后数据正确 | R4.1 |
| B.5 | `direct_reconnect_retry_backoff` | 用 `set_sys_now()` 逐步推进时间，验证重连退避间隔递增（start→2x→max） | 重试间隔递增 | R5.6 |
| B.6 | `direct_retry_exceed_limit_fail` | 用 `set_sys_now()` 推进时间触发重试超限 | pending 消息失败回调、handle 被移除 | R3.3 |
| B.7 | `direct_topology_offline_timeout` | 拓扑下线 → `set_sys_now()` 推进到 lost_topology_timeout 之后 → handle 移除 | 失败回调被调用 | - |
| B.8 | `direct_prefer_direct_over_upstream` | 有上游但允许直连，验证走直连路径而非上游转发 | 直连 atbus endpoint 存在、未经上游 | R3.4 |
| B.9 | `direct_prefer_direct_wait_discovery` | 有上游但允许直连，目标服务发现不存在 → 等待 → 到达后直连 | 直连成功 | R3.5 |

---

### C. 下游节点数据发送测试

**新建文件**: `atapp_downstream_send_test.cpp`

> 拓扑结构: `upstream(0x301) <--proxy-- downstream(0x302)`
> upstream 向 downstream 发消息

| # | 用例名 | 描述 | 验证点 | 需求 |
|----|--------|------|--------|------|
| C.1 | `downstream_not_connected_pending` | 下游未连接，消息排队等待 | 连接后 pending 消息到达 | - |
| C.2 | `downstream_connected_send_success` | 下游已连接发送成功 | 数据正确到达 | - |
| C.3 | `downstream_reconnect_before_timeout` | 下游断开 → 在 `set_sys_now()` 推进超时前重连 → pending 消息送达 | 消息发送成功 | R4.1 |
| C.4 | `downstream_timeout_without_reconnect` | 下游断开 → `set_sys_now()` 推进超时 → handle 移除 | pending 消息失败 | - |
| C.5 | `downstream_topology_offline_pending_fail` | 拓扑下线 → `set_sys_now()` 推进到 lost_topology_timeout 之后 → 失败 | 失败回调被调用 | - |

---

### D. 拓扑变更与丢失拓扑测试

**新建文件**: `atapp_topology_change_test.cpp`

> 核心测试 `update_topology_peer` 和 `remove_topology_peer` 的异步流程
>
> 拓扑结构: `node1(0x401) --proxy--> old_upstream(0x402)`，可选 `new_upstream(0x403)`, `target(0x404)`

| # | 用例名 | 描述 | 验证点 | 需求 |
|----|--------|------|--------|------|
| D.1 | `topology_change_new_upstream_not_connected` | update_topology_peer 切到新上游，新上游有服务发现但未连接 → 发起连接后正常 | 消息经新上游到达 | R1.1 |
| D.2 | `topology_change_new_upstream_already_connected` | update_topology_peer 切到新上游，新上游已有 atbus endpoint → 无缝切换 ready | handle 保持 ready，消息不中断 | R1.2 |
| D.3 | `topology_change_discovery_missing_then_arrive` | update_topology_peer 切到新上游 → 新上游无服务发现 → kWaitForDiscoveryToConnect → discovery 到达 → 连接成功 | 消息最终到达 | R1.3 |
| D.4 | `topology_change_discovery_timeout_cleanup` | update_topology_peer → 新上游无服务发现 → 用 `set_sys_now()` 推进时间触发重连超限 → handle 移除 | pending 消息失败 | R1.4 |
| D.5 | `topology_lost_recover_before_timeout` | remove_topology_peer → kLostTopology=true → 在 `set_sys_now()` 推进超时前 update_topology_peer 恢复 → kLostTopology 清除 | 通信不受影响 | R1.5 |
| D.6 | `topology_lost_timeout_cleanup` | remove_topology_peer → `set_sys_now()` 推进到 lost_topology_timeout 之后 → handle 强制移除 | pending 消息失败，下游也清理 | R1.6 |
| D.7 | `topology_lost_ready_handle_force_remove` | handle 处于 kReady+kLostTopology → `set_sys_now()` 推进到超时后仍强制移除 | handle 被移除（安全网机制） | - |
| D.8 | `topology_same_upstream_noop` | update_topology_peer 上游 ID 不变 → 仅尝试 try_direct_reconnect | handle 状态不变 | - |
| D.9 | `topology_proxy_in_new_upstream_path` | 当前 proxy_bus_id 仍在新上游链路中 → 不重算 → 保持现有代理 | handle 状态不变 | - |

---

### E. 服务发现生命周期与重连机制测试

**新建文件**: `atapp_discovery_reconnect_test.cpp`

> 测试 `on_discovery_event`、`set_handle_waiting_discovery`、`resume_handle_discovery` 的完整流程
> 以及 `setup_reconnect_timer` 的指数退避和计数行为
>
> 拓扑结构: `node1(0x501) ←→ node2(0x502)` (直连或上游关系)

| # | 用例名 | 描述 | 验证点 | 需求 |
|----|--------|------|--------|------|
| E.1 | `discovery_delete_then_put_reconnect` | on_discovery_event(kDelete) → kWaitForDiscoveryToConnect → on_discovery_event(kPut) → resume_handle_discovery → 重连成功 | 连接恢复，消息送达 | R5.1 |
| E.2 | `discovery_missing_reconnect_count_accumulate` | 服务发现缺失 → 用 `set_sys_now()` 逐步推进时间触发定时器 → reconnect_retry_times 递增 → 超限后 handle 移除 | handle 被清理 | R5.2 |
| E.3 | `reconnect_exponential_backoff` | 配置 start_interval=2s, max_interval=16s → 断开连接 → 用 `set_sys_now()` 逐步推进时间，验证重连间隔 2s→4s→8s→16s→16s | 间隔递增到 max 后不再增加 | R5.6 |
| E.4 | `reconnect_timer_replaced_by_earlier` | update_timer 若新 timeout < 旧 timeout → 定时器被替换 | 更早触发 | - |
| E.5 | `reconnect_timer_skip_if_later` | update_timer 若新 timeout > 旧 timeout → 定时器保持不变 | 原定时器不受影响 | - |

---

### F. 发送失败恢复与代理级联测试

**新建文件**: `atapp_error_recovery_test.cpp`

> 测试 `on_send_forward_request` 失败修复和代理 ready/unready 级联传播
>
> 拓扑结构: `node1(0x601) --proxy--> proxy(0x602) --proxy_for--> downstream(0x603)`

| # | 用例名 | 描述 | 验证点 | 需求 |
|----|--------|------|--------|------|
| F.1 | `send_fail_reconnect_then_success` | 发送返回 NO_CONNECTION → handle unready → endpoint 重建 → 重连 → 再次发送成功 | 第二次发送成功 | R5.3 |
| F.2 | `proxy_ready_cascade_downstream` | 代理节点 set_handle_ready → 被代理下游节点也 ready + add_waker 被调用 | 下游的 pending 消息被发送 | R5.4 |
| F.3 | `proxy_unready_cascade_downstream` | 代理节点 set_handle_unready → 下游节点也 unready | 下游 app_handle 也 unready | R5.4 |
| F.4 | `proxy_removed_downstream_closed` | remove_connection_handle(proxy) → 下游 handle 的 on_close_connection 被调用 | 下游 endpoint 关闭 | R5.5 |
| F.5 | `proxy_removed_downstream_no_app_handle` | 代理被移除 → 下游 handle 无 app_handle（纯代理节点）→ set_handle_unready + remove_connection_handle | handle 安全清理 | R5.5 |

---

### 需要新建的配置文件

> 测试配置使用略低于默认值的定时器间隔，正常测试流程中不会意外触发超时:
> - `reconnect_start_interval: 2s`（默认 8s）
> - `reconnect_max_interval: 16s`（默认 60s）
> - `reconnect_max_try_times: 3`（默认 5）
> - `lost_topology_timeout: 32s`（默认 120s）
>
> 需要验证超时/重连退避行为时，使用 `app::set_sys_now()` 推进时间。

#### A 组 — 上游转发测试

| 文件名 | 用途 | ID | 端口 | proxy |
|--------|------|-----|------|-------|
| `atapp_test_upstream_1.yaml` | 子节点1 | 0x00000101 | 21601 | `ipv4://127.0.0.1:21602` |
| `atapp_test_upstream_2.yaml` | 上游(中转) | 0x00000102 | 21602 | - |
| `atapp_test_upstream_3.yaml` | 子节点3 | 0x00000103 | 21603 | `ipv4://127.0.0.1:21602` |

#### B 组 — 直连测试

| 文件名 | 用途 | ID | 端口 | proxy | 直连规则 |
|--------|------|-----|------|-------|----------|
| `atapp_test_direct_1.yaml` | 对等节点1 | 0x00000201 | 21701 | `ipv4://127.0.0.1:21703` | allow_direct_connection: true |
| `atapp_test_direct_2.yaml` | 对等节点2 | 0x00000202 | 21702 | `ipv4://127.0.0.1:21703` | allow_direct_connection: true |
| `atapp_test_direct_3.yaml` | 共享上游 | 0x00000203 | 21703 | - | allow_direct_connection: true |

#### C 组 — 下游测试

| 文件名 | 用途 | ID | 端口 | proxy |
|--------|------|-----|------|-------|
| `atapp_test_downstream_1.yaml` | 上游节点 | 0x00000301 | 21801 | - |
| `atapp_test_downstream_2.yaml` | 下游节点 | 0x00000302 | 21802 | `ipv4://127.0.0.1:21801` |

#### D 组 — 拓扑变更测试

| 文件名 | 用途 | ID | 端口 | proxy |
|--------|------|-----|------|-------|
| `atapp_test_topo_1.yaml` | 子节点 | 0x00000401 | 21901 | `ipv4://127.0.0.1:21902` |
| `atapp_test_topo_2.yaml` | 旧上游 | 0x00000402 | 21902 | - |
| `atapp_test_topo_3.yaml` | 新上游 | 0x00000403 | 21903 | - |
| `atapp_test_topo_4.yaml` | 远端目标 | 0x00000404 | 21904 | `ipv4://127.0.0.1:21902` |

#### E 组 — 服务发现 & 重连测试

| 文件名 | 用途 | ID | 端口 | proxy |
|--------|------|-----|------|-------|
| `atapp_test_discovery_1.yaml` | 节点1 | 0x00000501 | 22001 | - |
| `atapp_test_discovery_2.yaml` | 节点2 | 0x00000502 | 22002 | - |

#### F 组 — 错误恢复 & 代理级联测试

| 文件名 | 用途 | ID | 端口 | proxy |
|--------|------|-----|------|-------|
| `atapp_test_recovery_1.yaml` | 节点1 | 0x00000601 | 22101 | - |
| `atapp_test_recovery_2.yaml` | 代理节点 | 0x00000602 | 22102 | - |
| `atapp_test_recovery_3.yaml` | 目标节点 | 0x00000603 | 22103 | `ipv4://127.0.0.1:22102` |

---

## 执行顺序

1. **Phase 1**: libatbus 通道层 & Node层回调测试 ✅ 已完成
   - [x] ios通道 kWritten 回调测试
   - [x] 关闭连接回调 (close_connection)
   - [x] 拓扑相关回调 (topology_update_upstream)

2. **Phase 2**: libatapp 配置文件准备
   - [ ] 创建 A 组配置 (upstream_1/2/3.yaml)
   - [ ] 创建 B 组配置 (direct_1/2/3.yaml)
   - [ ] 创建 C 组配置 (downstream_1/2.yaml)
   - [ ] 创建 D 组配置 (topo_1/2/3/4.yaml)
   - [ ] 创建 E 组配置 (discovery_1/2.yaml)
   - [ ] 创建 F 组配置 (recovery_1/2/3.yaml)

3. **Phase 3**: libatapp 上游转发测试
   - [ ] 创建 `atapp_upstream_forward_test.cpp`
   - [ ] 实现 A.1~A.8 用例
   - [ ] 编译 & 运行验证

4. **Phase 4**: libatapp 直连测试
   - [ ] 创建 `atapp_direct_connect_test.cpp`
   - [ ] 实现 B.1~B.9 用例
   - [ ] 编译 & 运行验证

5. **Phase 5**: libatapp 下游测试
   - [ ] 创建 `atapp_downstream_send_test.cpp`
   - [ ] 实现 C.1~C.5 用例
   - [ ] 编译 & 运行验证

6. **Phase 6**: libatapp 拓扑变更测试
   - [ ] 创建 `atapp_topology_change_test.cpp`
   - [ ] 实现 D.1~D.9 用例
   - [ ] 编译 & 运行验证

7. **Phase 7**: libatapp 服务发现 & 重连机制测试
   - [ ] 创建 `atapp_discovery_reconnect_test.cpp`
   - [ ] 实现 E.1~E.5 用例
   - [ ] 编译 & 运行验证

8. **Phase 8**: libatapp 错误恢复 & 代理级联测试
   - [ ] 创建 `atapp_error_recovery_test.cpp`
   - [ ] 实现 F.1~F.5 用例
   - [ ] 编译 & 运行验证

---

## 覆盖率检查矩阵

> 确保所有 `atapp_connector_atbus` 关键函数均有对应测试路径

| 函数 | 测试覆盖 |
|------|----------|
| `on_start_connect` | A.1, A.2, B.1, B.3, C.2 |
| `on_close_connection` | A.5, B.6, F.4 |
| `on_send_forward_request` (失败路径) | F.1 |
| `try_connect_to` | A.1~A.8, B.1~B.9, C.1~C.5 |
| `on_start_connect_to_connected_endpoint` | A.2, B.3, C.2 |
| `on_start_connect_to_same_or_other_upstream_peer` | B.1, B.2, B.8, B.9 |
| `on_start_connect_to_upstream_peer` | A.1, A.2, A.4 |
| `on_start_connect_to_downstream_peer` | C.1, C.2, C.3 |
| `on_start_connect_to_proxy_by_upstream` | A.2, A.8 |
| `setup_reconnect_timer` (退避) | B.5, E.3 |
| `setup_reconnect_timer` (超限移除) | A.5, B.6, E.2 |
| `try_direct_reconnect` | A.4, B.4, C.3, D.1 |
| `set_handle_lost_topology` | A.7, B.7, C.5, D.5, D.6, D.7 |
| `update_topology_peer` | D.1~D.9 |
| `remove_topology_peer` | D.5, D.6, D.7 |
| `set_handle_waiting_discovery` | B.2, D.3, E.1 |
| `resume_handle_discovery` | A.1, B.2, D.3, E.1 |
| `set_handle_ready` (级联) | F.2 |
| `set_handle_unready` (级联) | F.3 |
| `update_timer` (替换/跳过) | E.4, E.5 |
| `remove_connection_handle` (代理清理) | F.4, F.5 |
| `bind_connection_handle_proxy` | D.1, D.2, B.8, F.2 |
| `unbind_connection_handle_proxy` | D.1 (重算链路), F.4 |
| `on_update_endpoint` | A.2, B.3, C.2 |
| `on_remove_endpoint` | A.4, B.4, C.3, D.6 |
| `on_discovery_event` (kPut) | A.1, B.2, E.1 |
| `on_discovery_event` (kDelete) | E.1 |
