# Item Guide

Use for ItemAlgorithm containers, Lobby item operations, dirty sync, and operation-source propagation. Public behavior
is in `doc/docs/components/item.md`.

## Authoritative sources

- `src/component/GameSharedComponent/ItemAlgorithm/include/ItemAlgorithm/ItemContainer.h` and
  `ItemContainerTypes.h`: container contracts, `ItemOperationReason`, `ItemOperationSource`, and context.
- `src/component/GameSharedComponent/ItemAlgorithm/src/ItemAlgorithm/ItemContainerOperation.cpp`: callback delivery.
- `src/lobbysvr/service/logic/item/user_item_manager.cpp`: multi-container check/do flow.
- `src/lobbysvr/service/logic/item/user_item_container.cpp` and `user_item_container_manager.cpp`: Lobby callbacks and
  dirty sync.
- `src/server_frame/protocol/public/protocol/pbdesc/com.const.proto`, `com.struct.item.common.proto`, and
  `src/server_frame/protocol/private/protocol/log/svr.oss.log.proto`: distinct source and OSS schemas.

## Review checks

- Keep mechanism reasons separate from business sources. `ItemOperationReason` describes add/sub/load/move/apply;
  `ItemOperationSource` carries `major_reason`, `minor_reason`, and `reason_param` from the caller.
- Multi-container check/do is not atomic. A do-stage failure does not roll back earlier containers; review every
  caller's compensation or idempotency strategy. Mail attachments mark committed attachments received.
- Sources reach ItemAlgorithm callbacks, but Lobby currently discards context in item-changed/count-changed callbacks.
  Dirty sync carries container/count changes, not operation sources.
- Mail attachments currently set `EN_ITEM_CHG_MAJOR_MAIL_ATTACHMENT`, mail minor type, and mail ID. Do not assume all
  other operations provide non-`UNKNOWN` sources.
- `EnItemFlowReasonMajorType` is distinct from `EnItemChgMajorType` and currently has only `INVALID`. `OSSItemFlow`
  has a schema but no emitter under `src/`; never describe context propagation as completed OSS item-flow output.
- For new OSS emission, verify field mapping, partial failures, retry duplication, and the actual log sink. For client
  changes, test payload and delivery rather than only manager state.
