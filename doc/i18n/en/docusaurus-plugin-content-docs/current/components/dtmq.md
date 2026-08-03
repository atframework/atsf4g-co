---
title: Distributed Message Queue (dtmq)
---

# Distributed Message Queue (dtmq)

dtmq provides high-frequency in-game message channels (chat, team, guild broadcasts, etc.): channel
subscription, message send/receive, historical message fetching, plus WAL-based master-slave replication and
channel migration.

Location: `src/component/dtmq/` (see design notes in `dtmq-proxysvr/Note.md`).

## Composition

| Part | Location | Description |
| --- | --- | --- |
| `dtmq-proxysvr` | `dtmq/dtmq-proxysvr/` | Entry service: channel management, message persistence, subscriber distribution (in the current version the data layer is integrated; TODO: split it out) |
| Protocol | `dtmq/protocol/` | `dtmq_proxy.proto` (service protocol), `dtmq_proxy.config.proto` (configuration) |
| `dtmq-common-sdk` | `dtmq/sdk/common/` | Channel hashing/replica selection algorithms (`dtmq_algorithm`) |
| `dtmq-proxy-sdk` | `dtmq/sdk/proxy/` | Client API and in-process subscriber |

## Service Protocol (DtmqProxysvrService)

`dtmq_proxy.proto` (package `atframework.dtmq`, module_name `"dtmq"`), all with `allow_no_wait: true`:

`subscribe` / `unsubscribe` / `send_message` / `transfer_channel` / `destroy_channel` / `update` /
`reset_lock` / `find_message` / `page_query_message` / `pull`.

There is also `DtmqProxysvrNotifyService::channel_event_sync(stream SSChannelEventSync)`: the server
**streams** incremental channel messages / full snapshots to the nodes where subscribers reside.

## Data Model

- Channel structures are defined in the public protocol
  `src/server_frame/protocol/public/protocol/pbdesc/com.struct.dtmq.proto`:
  `DChannelMetadata` / `DChannelRuntime` / `DChannelMessage(Detail)` / `DChannelSnapshot` /
  `DChannelSubscribeNode` / `DChannelSyncPoint`;
- Subscriber keys look like `U:{zone}:{user}` / `T:{team}` / `G:{guild}`, carrying a heartbeat sequence +
  hash;
- Optimistic locking: `channel_lock_checker` (CAS), unlocked via `reset_lock`;
- Channel type configuration comes from Excel (`ExcelDtmqChannelType` in `com.struct.dtmq.config.proto`:
  `channel_type`, `show_max_log_count`, `readonly_replicate_count`), loaded by
  `excel_config_dtmq_index.cpp` as `DChannelConfigure`;
- DB persistence: `table_dtmq_channel_record` (the generated `rpc::db::dtmq_channel_record` Redis
  interface).

## Replicas and Routing

```mermaid
flowchart LR
    Pub[Publisher] -->|send_message| W[Writable Replica]
    W -->|WAL sync| R1[Readonly Replica 1]
    W -->|WAL sync| R2[Readonly Replica N]
    Sub[Subscriber SDK] -->|pull / event stream| R1
    Sub -->|pull / event stream| W
```

- Each channel has 1 writable replica + N readonly replicas (`readonly_replicate_count` is configurable);
- Target node selection: `rpc::dtmq::get_target_server_id(s)` hashes by channel key + HPA ready discovery
  (`logic_hpa_discovery_select_mode::kReady`);
- When readonly and writable are on the same node, writable takes precedence (except when
  `replicate_index > 0`);
- Supports channel migration (`transfer_channel`), subscriber merging/unsubscription during scale-out/in,
  writable⇄readonly promotion/demotion, and forwarding during the migration window.

## WAL Master-Slave Sync

`dtmq-proxysvr/data/mq_channel_wal_handle.{h,cpp}` wraps atframe_utils'
`distributed_system::wal_publisher / wal_subscriber` (single-thread mode); logs are merged by
`DChannelMessageDetail::CommandCase` category. This is the same mechanism as the rank component's
`rank_wal_handle`. `SSChannelUpdateReq` supports log compaction via `compact_sequence`.

## Client SDK

- `dtmq_client_api`: `get_target_server_id(s)`, `send_message`, `find_message`, `page_query_message`,
  `normalize_replicate_index`;
- `dtmq_client_subscriber`: in-process shared subscriber (`shared_subscriber`): local WAL log cache,
  optimistic lock/snapshot/message callbacks, heartbeat, and receiving the `channel_event_sync` event
  stream.

## Business Integration Example (lobbysvr)

`src/lobbysvr/service/` links `dtmq-proxy-sdk`, generates handlers for `DtmqProxysvrNotifyService`
(`app/handle_ss_rpc_dtmqproxysvrnotifyservice.atfw.gen.*`), and calls
`register_handles_for_dtmqproxysvrnotifyservice()` in `lobbysvr_main.cpp`;
`logic/dtmq/task_action_channel_event_sync.*` receives the channel event stream and delivers it to clients.
