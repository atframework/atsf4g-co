---
title: Item Operations and Change Sources
---

# Item Operations and Change Sources

Generic container algorithms live in `src/component/GameSharedComponent/ItemAlgorithm/`; player integration lives in
`src/lobbysvr/service/logic/item/`. Containers own entries, counts, positions, checks, and notifications. Lobby owns
persistence, dirty synchronization, and concrete business sources.

## Container Operations

`ItemContainer` provides batch flows such as add/sub/load/move/apply_entries and hooks for per-entry validation,
placement, and field mapping. Finite-grid, infinite-grid, and no-position containers are supported.

`user_item_manager` performs `check_add/check_sub` for multiple containers before `do_operation`. Failure during the
execution phase returns immediately without rolling back containers already committed; check/do is not a
cross-container atomic transaction. Mail attachment receipt marks committed attachments as received to prevent
issuing them again.

## Operation Reasons and Business Sources

These concepts are distinct:

| Type | Contents | Purpose |
| --- | --- | --- |
| `ItemOperationReason` | `kAdd`, `kSub`, `kLoad`, `kMoveSub`, `kMoveAdd`, etc. | Mechanism reason determined by the container |
| `ItemOperationSource` | `major_reason`, `minor_reason`, `reason_param` | Business source supplied by the caller |
| `EnItemChgMajorType` | Use, sell, buy, mail attachment, and similar enums | Values for the primary business source |

`ItemOperationContext` carries both the container reason and business source. Sources propagate to ItemAlgorithm
callbacks such as `on_item_added`. Mail attachment receipt is the current production call site that explicitly sets a
non-`UNKNOWN` source: `EN_ITEM_CHG_MAJOR_MAIL_ATTACHMENT + mail.minor_type + mail_id`. Other major operations may
still use the default `UNKNOWN`; complete business attribution must not be assumed.

## Dirty-Sync Boundary

Lobby's `on_item_changed` and `on_item_count_changed` currently discard the operation context, and client dirty data
contains only containers and item counts. Sources reach algorithm callbacks but are not automatically delivered by
the existing dirty protocol, persisted, or emitted as telemetry by that mechanism alone.

## OSS Flow Boundary

`com.struct.item.common.proto` separately defines `EnItemFlowReasonMajorType`, `EnItemFlowReasonMinorType`, and
`EnItemFlowReason`; its major enum currently contains only `INVALID`. Although `svr.oss.log.proto` defines
`OSSItemFlow`, there is no emission point under `src/`.

`ItemOperationSource` is therefore a source-propagation interface, not a connected OSS item-flow system. New flow
emission must map business sources, container reasons, and OSS fields explicitly and verify the real log sink; filling
the context alone does not establish flow output.
