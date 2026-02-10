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

## libatapp 单元测试补全

### 项目信息

- **代码目录**: `atframework/libatapp`
  - ⚠️ 忽略 `atframework/libatapp/atframework` 内的所有内容
- **单元测试目录**: `atframework/libatapp/test/case`
- **单元测试编译和执行目标**: `atapp_unit_test`

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

---

## libatapp etcd 接入测试

### 架构概述

libatapp 通过 `etcd_module`（atapp 模块接口）和 `etcdcli/*` 库接入 etcd，实现服务注册、服务发现和拓扑管理。

**关键组件层次**:

```
etcd_module (atapp module 生命周期集成)
  ├─ etcd_cluster     (HTTP/JSON 连接管理、认证、租约、成员发现)
  ├─ etcd_keepalive   (注册 KV 并绑定租约 TTL)
  ├─ etcd_watcher     (监听 key 变化: 快照+长轮询流式watch)
  ├─ etcd_discovery   (内存中服务发现数据集: 一致性哈希、轮询、随机路由)
  └─ etcd_packer      (JSON/Base64 序列化工具)
```

**接入模式**: HTTP-only，使用 etcd v3 JSON gateway (非 gRPC)，通过 libcurl multi-handle 异步通信。

**使用的 etcd v3 API 端点**:

| 端点 | 用途 |
|------|------|
| `/v3/cluster/member/list` | 发现集群成员 |
| `/v3/auth/authenticate` | 用户名密码认证，获取 token |
| `/v3/auth/user/get` | 刷新认证 token |
| `/v3/lease/grant` | 获取租约 |
| `/v3/lease/keepalive` | 续约 |
| `/v3/kv/lease/revoke` | 撤销租约（停止时） |
| `/v3/kv/range` | 读取 KV |
| `/v3/kv/put` | 写入 KV |
| `/v3/kv/deleterange` | 删除 KV |
| `/v3/watch` | 长轮询 Watch |

**Key 路径结构**:

```
<etcd.path>/
├── by_id/<app_name>-<app_id>        # 按 ID 索引的服务发现
├── by_name/<app_name>-<app_id>      # 按 Name 索引的服务发现
└── topology/<app_name>-<app_id>     # 拓扑信息
```

---

### G. CI 脚本: etcd 服务管理

**新建目录**: `atframework/libatapp/ci/etcd/`

#### G.1 etcd 下载与启停脚本 (Bash)

**新建文件**: `atframework/libatapp/ci/etcd/setup-etcd.sh`

> 跨 Linux/macOS 平台脚本

**功能需求**:

| 子命令 | 功能 | 说明 |
|--------|------|------|
| `download` | 下载最新版本 etcd | 通过 GitHub API (`/repos/etcd-io/etcd/releases/latest`) 获取最新 tag，下载对应平台(linux-amd64/linux-arm64/darwin-amd64/darwin-arm64)的包 |
| `start` | 启动 etcd 实例 | 在指定 data-dir 启动，支持自定义 client/peer 端口，后台运行并记录 PID |
| `stop` | 停止 etcd 实例 | 通过 PID 文件停止已启动的实例 |
| `cleanup` | 清理数据和二进制 | 删除 data-dir 和下载的二进制 |
| `status` | 检查运行状态 | 检查 PID 文件和进程是否存活 |

**设计要点**:

```bash
# 目录结构 (运行时)
<WORK_DIR>/
├── etcd                    # etcd 二进制
├── etcdctl                 # etcdctl 二进制
├── etcd.pid                # PID 文件
├── etcd.log                # 日志文件
└── data/                   # etcd 数据目录
```

- **版本获取**: `curl -sSL https://api.github.com/repos/etcd-io/etcd/releases/latest | grep tag_name`
- **下载 URL 模式**:
  - Linux: `https://github.com/etcd-io/etcd/releases/download/${TAG}/etcd-${TAG}-linux-${ARCH}.tar.gz`
  - macOS: `https://github.com/etcd-io/etcd/releases/download/${TAG}/etcd-${TAG}-darwin-${ARCH}.zip`
- **架构检测**: `uname -m` → amd64/arm64
- **重复启停处理**: `start` 前检查 PID 文件，若进程仍存活则先 `stop`；`stop` 发送 SIGTERM，等待 5s，仍存活则 SIGKILL
- **健康检查**: 启动后轮询 `etcdctl endpoint health --endpoints=http://127.0.0.1:${CLIENT_PORT}` 直到 healthy 或超时
- **默认端口**: client=12379, peer=12380（避免与系统 etcd 冲突）

**参数**:

```
setup-etcd.sh <command> [options]
  --work-dir DIR        工作目录 (默认: /tmp/etcd-unit-test)
  --client-port PORT    客户端端口 (默认: 12379)
  --peer-port PORT      对等端口 (默认: 12380)
  --etcd-version VER    指定版本 (默认: latest)
```

#### G.2 etcd 下载与启停脚本 (PowerShell)

**新建文件**: `atframework/libatapp/ci/etcd/setup-etcd.ps1`

> Windows 平台脚本

**功能需求**: 与 Bash 版本一致

**设计要点**:

```powershell
# 目录结构 (运行时)
<WORK_DIR>\
├── etcd.exe                # etcd 二进制
├── etcdctl.exe             # etcdctl 二进制
├── etcd.pid                # PID 文件 (存储进程 ID)
├── etcd.log                # 日志文件
└── data\                   # etcd 数据目录
```

- **版本获取**: `Invoke-RestMethod -Uri https://api.github.com/repos/etcd-io/etcd/releases/latest | Select-Object -ExpandProperty tag_name`
- **下载 URL 模式**: `https://github.com/etcd-io/etcd/releases/download/${TAG}/etcd-${TAG}-windows-amd64.zip`
- **重复启停处理**: `start` 前通过 `Get-Process -Id (Get-Content etcd.pid)` 检查进程；`stop` 使用 `Stop-Process`
- **健康检查**: 启动后轮询 `etcdctl.exe endpoint health`
- **进程启动**: `Start-Process -NoNewWindow -PassThru -RedirectStandardOutput etcd.log`

**参数** (同 Bash):

```
setup-etcd.ps1 -Command <download|start|stop|cleanup|status>
  -WorkDir DIR          工作目录 (默认: $env:TEMP\etcd-unit-test)
  -ClientPort PORT      客户端端口 (默认: 12379)
  -PeerPort PORT        对等端口 (默认: 12380)
  -EtcdVersion VER      指定版本 (默认: latest)
```

---

### H. etcdcli 纯客户端层单元测试（不需要 etcd 服务）

> 以下测试不需要真实 etcd 服务，仅测试客户端逻辑

**新建文件**: `atframework/libatapp/test/case/atapp_etcd_packer_test.cpp`

#### H.1 etcd_packer 序列化/反序列化测试

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| H.1.1 | `packer_key_value_pack_unpack` | etcd_key_value 打包/解包往返 | key/value base64 编码正确、revision/version 一致 |
| H.1.2 | `packer_response_header_pack_unpack` | etcd_response_header 打包/解包 | cluster_id/member_id/revision/raft_term 正确 |
| H.1.3 | `packer_key_range_prefix` | pack_key_range 前缀匹配 ("+1") | 例如 key="abc" → range_end="abd" |
| H.1.4 | `packer_key_range_exact` | pack_key_range 精确匹配 (空 range_end) | range_end 为空 |
| H.1.5 | `packer_base64_roundtrip` | base64 编码/解码往返 | 各种长度字符串（空串/短串/长串/含特殊字符） |
| H.1.6 | `packer_unpack_int_formats` | unpack_int 处理数字和字符串格式 | etcd 返回 int64 有时为 JSON 数字，有时为字符串 |
| H.1.7 | `packer_parse_object_error` | parse_object 处理无效 JSON | 返回 false，不崩溃 |

#### H.2 etcd_discovery_set 内存路由测试

> 已有 `atapp_discovery_test.cpp` 覆盖 metadata_filter / get_discovery_by_metadata / round_robin / consistent_hash_compact / consistent_hash_unique 等

**新增测试** (追加到 `atapp_discovery_test.cpp`):

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| H.2.1 | `discovery_node_version_update` | 版本更新：同 ID 节点用更高 modify_revision 更新 | 旧节点被替换，路由数据更新 |
| H.2.2 | `discovery_node_version_stale_skip` | 版本过旧：用更低 modify_revision 尝试更新 | 被忽略，保持原节点 |
| H.2.3 | `discovery_add_remove_stress` | 大量（100+）节点增删 | 一致性哈希环一致性、无内存泄漏 |
| H.2.4 | `discovery_node_ingress_round_robin` | `next_ingress_gateway()` 轮询 gateway 地址 | 返回地址依次循环 |
| H.2.5 | `discovery_empty_set_operations` | 空集合上的所有查询操作 | 返回 nullptr/空，不崩溃 |

---

### I. etcd 集成测试（需要 etcd 服务）

> 以下测试需要真实 etcd 服务运行（通过 G 组 CI 脚本启动）
> 所有测试需在开头检测 etcd 是否可用，不可用时跳过 (CASE_MSG_INFO() + return)

**新建文件**: `atframework/libatapp/test/case/atapp_etcd_cluster_test.cpp`

**辅助函数** (文件开头):

```cpp
// 检测 etcd 是否可用
static bool is_etcd_available() {
  // 检查环境变量 ATAPP_UNIT_TEST_ETCD_HOST (默认 "http://127.0.0.1:12379")
  // 尝试 HTTP GET /version 或 /health
  // 返回 true/false
}

// 获取 etcd 主机地址
static std::string get_etcd_host() {
  const char* env = getenv("ATAPP_UNIT_TEST_ETCD_HOST");
  return env ? env : "http://127.0.0.1:12379";
}

// 创建配置好的 etcd_cluster 对象
static std::shared_ptr<atapp::etcd_cluster> create_test_cluster();
```

#### I.1 etcd_cluster 集群管理测试

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| I.1.1 | `cluster_init_and_connect` | 初始化 etcd_cluster，tick 直到 kRunning+kReady | `is_available()` 返回 true，`get_lease() != 0` |
| I.1.2 | `cluster_member_list_discovery` | tick 后 `get_available_hosts()` 包含至少一个成员 | hosts 非空 |
| I.1.3 | `cluster_lease_grant_and_keepalive` | 获取 lease 后等待一个 keepalive 周期 | `get_lease()` 保持不变（续约成功） |
| I.1.4 | `cluster_close_revoke_lease` | `close(true, true)` → 等待关闭完成 | lease 被撤销，关联的 KV 被自动删除 |
| I.1.5 | `cluster_stats_tracking` | 发送若干请求后检查 `get_stats()` | `sum_create_requests > 0`, `sum_success_requests > 0` |
| I.1.6 | `cluster_event_up_down_callbacks` | 注册 `add_on_event_up` / `add_on_event_down` | up 回调在 ready 时触发，down 回调在 close 时触发 |

#### I.2 etcd_keepalive KV 注册测试

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| I.2.1 | `keepalive_set_value_and_read` | 创建 keepalive 设置值 → 用 `create_request_kv_get` 读回 | 值一致 |
| I.2.2 | `keepalive_update_value` | 更新 keepalive 值 → 再读回 | 新值 |
| I.2.3 | `keepalive_lease_binding` | 停止 keepalive → 等待 lease TTL 过期 → 读取 | KV 被自动删除 |
| I.2.4 | `keepalive_checker_conflict` | 两个 keepalive 注册同一 path → 第二个 checker 应检测到冲突 | 第二个不覆盖 |
| I.2.5 | `keepalive_checker_same_identity` | 同 identity 的 keepalive 重启注册同一 path | checker 通过，允许覆盖 |
| I.2.6 | `keepalive_remove_and_readd` | remove_keepalive → add_keepalive | 重新注册成功 |

#### I.3 etcd_watcher Watch 事件测试

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| I.3.1 | `watcher_initial_snapshot` | 预写入若干 KV → 创建 watcher → 收到 snapshot 事件 | `response.snapshot == true`，kv 列表完整 |
| I.3.2 | `watcher_put_event` | watcher 运行中 → 写入新 KV | 收到 kPut 事件，kv.key/value 正确 |
| I.3.3 | `watcher_delete_event` | watcher 运行中 → 删除 KV | 收到 kDelete 事件 |
| I.3.4 | `watcher_prefix_range` | 创建前缀 watcher(`key+"+1"`) → 写入多个前缀下的 KV | 只收到匹配前缀的事件 |
| I.3.5 | `watcher_reconnect_after_timeout` | 等待 watch 超时（默认 1 小时，可配短于 watcher_request_timeout） → 自动重连 | 重连后继续收到事件 |
| I.3.6 | `watcher_revision_continuity` | 写入多个 KV → 验证 `last_revision` 单调递增 | revision 递增 |

#### I.4 etcd_module 模块集成测试

> 测试 etcd_module 作为 atapp 模块的完整集成流程
> 需要构造 atapp 实例并加载 etcd_module

**新建配置文件**: `atframework/libatapp/test/case/atapp_test_etcd.yaml`

```yaml
atapp:
  id: 0x00000701
  name: "etcd-test-node"
  bus:
    listen: "ipv4://127.0.0.1:22201"
  etcd:
    enable: true
    hosts:
      - "http://127.0.0.1:12379"
    path: "/unit-test/libatapp"
    init:
      timeout: "10s"
      tick_interval: "64ms"
    keepalive:
      timeout: "16s"
      ttl: "5s"
    request:
      timeout: "5s"
      initialization_timeout: "3s"
    watcher:
      retry_interval: "3s"
```

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| I.4.1 | `etcd_module_init_and_ready` | 启动 atapp 带 etcd_module → etcd_ctx 进入 ready | `is_etcd_enabled()`, `get_raw_etcd_ctx().is_available()` |
| I.4.2 | `etcd_module_keepalive_paths` | 模块初始化后检查 keepalive 路径 | `get_discovery_by_id_path()`, `get_discovery_by_name_path()`, `get_topology_path()` 非空 |
| I.4.3 | `etcd_module_watcher_paths` | 模块初始化后检查 watcher 路径 | `get_discovery_by_id_watcher_path()`, `get_discovery_by_name_watcher_path()`, `get_topology_watcher_path()` 非空 |
| I.4.4 | `etcd_module_discovery_registration` | 启动两个 atapp 实例 → 都带 etcd_module → 互相通过 etcd 发现 | node1 能在 `get_global_discovery()` 中找到 node2 |
| I.4.5 | `etcd_module_topology_registration` | 启动后检查 `get_topology_info_set()` | 包含自身拓扑信息 |
| I.4.6 | `etcd_module_discovery_event_callback` | 注册 `add_on_node_discovery_event` → 启动第二个节点 → 回调被触发 | 回调参数: action=kPut, node 非空 |
| I.4.7 | `etcd_module_discovery_event_remove_callback` | 注册回调 → `remove_on_node_event` 移除 → 启动第二个节点 → 回调不再触发 | handle 置为 end()，新节点上线不触发已移除回调 |
| I.4.8 | `etcd_module_discovery_event_delete` | 注册回调 → 启动第二个节点(触发 kPut) → 关闭第二个节点(触发 kDelete) | 先收到 kPut（node 非空），再收到 kDelete（node 为关闭的节点） |
| I.4.9 | `etcd_module_discovery_event_multi_callbacks` | 注册多个回调 → 启动第二个节点 | 所有回调均被调用，回调顺序与注册顺序一致 |
| I.4.10 | `etcd_module_topology_event_callback` | 注册 `add_on_topology_info_event` → 启动第二个节点 → 回调被触发 | 回调参数: action=kPut, topology_info 非空, version.create_revision > 0 |
| I.4.11 | `etcd_module_topology_event_remove_callback` | 注册回调 → `remove_on_topology_info_event` 移除 → 启动第二个节点 → 回调不再触发 | handle 置为 end()，新节点拓扑上线不触发已移除回调 |
| I.4.12 | `etcd_module_topology_event_delete` | 注册回调 → 启动第二个节点(触发 kPut) → 关闭第二个节点(租约过期后触发 kDelete) | 先收到 kPut，再收到 kDelete |
| I.4.13 | `etcd_module_topology_event_multi_callbacks` | 注册多个拓扑回调 → 启动第二个节点 | 所有回调均被调用 |
| I.4.14 | `etcd_module_discovery_snapshot` | 启动后 `has_discovery_snapshot()` 为 true | 快照加载完成 |
| I.4.15 | `etcd_module_topology_snapshot` | 启动后 `has_topology_snapshot()` 为 true | 快照加载完成 |
| I.4.16 | `etcd_module_stop_revoke_lease` | stop 模块 → 等待 → 关联 KV 被删除 | etcd 中无该节点 KV |
| I.4.17 | `etcd_module_reload_config` | 修改配置 → reload → 参数生效 | timeout 等配置更新 |
| I.4.18 | `etcd_module_disable_enable` | `disable_etcd()` → tick → `enable_etcd()` → tick → 恢复 | 关闭后不通信，重新启用后恢复 |

#### I.5 多节点服务发现联动测试

> 利用 etcd 作为中间件，测试多个 atapp 节点通过 etcd 实现服务发现的全流程

**新建配置文件**:
- `atframework/libatapp/test/case/atapp_test_etcd_node1.yaml` (ID: 0x00000801, port: 22301)
- `atframework/libatapp/test/case/atapp_test_etcd_node2.yaml` (ID: 0x00000802, port: 22302)

| # | 用例名 | 描述 | 验证点 |
|----|--------|------|--------|
| I.5.1 | `multi_node_discovery_put_event` | node1 启动 → node2 启动 → node1 收到 node2 的 discovery kPut | node1 的 global_discovery 包含 node2 |
| I.5.2 | `multi_node_discovery_delete_event` | node2 关闭 → node1 收到 kDelete 事件 | node1 的 global_discovery 不再包含 node2 |
| I.5.3 | `multi_node_topology_update` | 两个节点都注册拓扑 → 互相能看到对方拓扑 | topology_info_set 包含对方 |
| I.5.4 | `multi_node_custom_data` | `set_conf_custom_data` → 另一节点通过 watcher 收到 | custom_data 正确传播 |

---

### 需要新建/修改的配置文件汇总 (etcd 测试)

#### I 组 — etcd 集成测试

| 文件名 | 用途 | ID | 端口 | etcd 配置 |
|--------|------|-----|------|-----------|
| `atapp_test_etcd.yaml` | 单节点 etcd 模块测试 | 0x00000701 | 22201 | hosts: `["http://127.0.0.1:12379"]`, path: `/unit-test/libatapp` |
| `atapp_test_etcd_node1.yaml` | 多节点测试节点1 | 0x00000801 | 22301 | 同上 |
| `atapp_test_etcd_node2.yaml` | 多节点测试节点2 | 0x00000802 | 22302 | 同上 |

---

### etcd 测试执行顺序

1. **Phase E1**: CI 脚本（无需编译）
   - [ ] 创建 `ci/etcd/setup-etcd.sh`
   - [ ] 创建 `ci/etcd/setup-etcd.ps1`
   - [ ] 本地验证: download → start → health check → stop → cleanup

2. **Phase E2**: 纯客户端测试（不需要 etcd）
   - [ ] 创建 `atapp_etcd_packer_test.cpp` (H.1.1~H.1.7)
   - [ ] 追加 `atapp_discovery_test.cpp` (H.2.1~H.2.5)
   - [ ] 编译 & 运行验证

3. **Phase E3**: 启动 etcd，运行集成测试
   - [ ] 创建 `atapp_etcd_cluster_test.cpp` (I.1~I.3)
   - [ ] 创建 etcd 测试配置文件（I 组）
   - [ ] 启动 etcd 服务(`setup-etcd.sh start` / `setup-etcd.ps1 -Command start`)
   - [ ] 编译 & 运行验证
   - [ ] 停止 etcd 服务

4. **Phase E4**: etcd_module 集成测试
   - [ ] 创建 `atapp_etcd_module_test.cpp` (I.4.1~I.4.11, I.5.1~I.5.4)
   - [ ] 编译 & 运行验证
