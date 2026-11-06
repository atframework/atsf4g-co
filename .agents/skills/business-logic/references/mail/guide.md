# Mail Guide

Use for Lobby user/global mail, templates, delivery SDKs, CS sync, attachments, and mail-item sources. Mail is part of
`lobbysvr`; there is no separate mail service. Public behavior is in `doc/docs/components/mail.md`.

## Authoritative sources

- `src/lobbysvr/sdk/rpc/mail/user_mail.h/.cpp` and `global_mail.h/.cpp`: delivery/removal APIs and DB ordering.
- `src/lobbysvr/sdk/logic/mail/mail_utils.h/.cpp`: template rendering, validity, expiration, and record merging.
- `src/lobbysvr/service/logic/mail/user_mail_manager.h/.cpp` and `global_mail_manager.h/.cpp`: user state, CS sync,
  global refresh, and attachment receipt.
- `resource/ExcelTables/Mail.xlsx`: template definitions and `render_by_client` usage.
- `src/server_frame/protocol/public/protocol/pbdesc/com.protocol.proto`: actual CS mail surface.

## Review checks

- Consume SDK results with the project RPC await macros. Personal delivery writes content before adding the user
  async index job; duplicate index insertion cleans up newly unlinked content and is idempotent success. Preserve the
  fallback TTL.
- Global indexes use `(zone_id, major_type)`; `zone_id=0` means cross-zone. Check capacity compaction, retry/version
  handling, Lobby refresh, and the internal `return_updated=false` path's local-cache requirement.
- Both template APIs include an `after_rendered` overload. It runs synchronously after rendering and mail-ID
  allocation, before persistence. Keep the `function_ref` callable alive for the call and never retain it.
- `render_by_client` selects rendering ownership. Verify actual `${var}`, `${format(...)}`, nested-function, and
  custom-title/body outputs. Template IDs use `[600000, 700000)`.
- The CS surface is get-all, read/read-all, receive-one/receive-all, and change sync. Do not infer additional client
  APIs from generator capabilities.
- Attachments reject instance `data`. Multi-container receipt can partially commit; mark committed attachments as
  received and never reissue them through retry. Default `need_remove=true` also marks empty operations removed.
- Successful attachment receipt uses `EN_ITEM_CHG_MAJOR_MAIL_ATTACHMENT`, mail `minor_type`, and `mail_id` as the
  item source. This source propagation is not an OSS flow emitter. Rank settlement does not automatically deliver
  mail or items until an integrator implements that flow.
