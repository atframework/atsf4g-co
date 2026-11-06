---
title: Mail
---

# Mail

Mail belongs to `lobbysvr` and `lobbysvr-sdk`; there is no separate `mailsvr`. It includes user/global mail
managers, Redis records, `Mail.xlsx` templates, delivery SDKs, CS synchronization, and robot commands.

## Delivery SDK

`src/lobbysvr/sdk/rpc/mail/` provides these RPC adapter APIs; consume their return values through the project's RPC
await macros:

- `add_user_mail` and `add_user_mail_with_template`;
- `add_global_mail` and `add_global_mail_with_template`;
- `remove_global_mail`.

Both template-delivery APIs have an `after_rendered` overload. The callback runs synchronously after template
rendering and mail-ID allocation but before persistence, and may modify `DMailContent` to customize the title, body,
or other content. Its type is `function_ref`; the callable must remain valid for this call and must not be retained
for later use.

Consumers use the existing `USE_SERVICE_SDK "lobbysvr-sdk"` dependency. User mail requires a valid `major_type` and
receiver user ID. Missing time fields are filled from delivery time and the default expiration configuration.

## Storage and Refresh

User mail writes `mail_content` before inserting the mail index through a user async job. If the index already
exists, the newly unlinked content is cleaned up and the operation is treated as idempotent success. Content records
receive a fallback TTL to prevent leaks on exceptional paths.

Global mail uses `mail_global_index` and content records, with indexes managed by `(zone_id, major_type)` and
capacity compaction. `zone_id=0` denotes cross-zone global mail. Changes notify relevant Lobby instances to refresh;
the internal `return_updated=false` path refreshes only other Lobby instances, so its caller maintains the local
cache.

## Templates

`resource/ExcelTables/Mail.xlsx` defines client and server templates. `render_by_client` selects the rendering side;
the server template engine supports `${var}`, `${format(...)}`, and nested functions. Template IDs occupy
`[600000, 700000)`; current examples include `600001` and `600002`.

The SDK template overloads expose fixed extension parameters. `mail_render_template` in
`src/lobbysvr/sdk/logic/mail/mail_utils.h` also supports an internal extension map and attachment parameters. When
adding syntax or fields, verify `mail_setup_template_engine` and actual rendering; a successful generator exit alone
is insufficient.

## CS APIs and Synchronization

Current client APIs are limited to:

- `mail_get_all`;
- `mail_read` and `mail_read_all`;
- `mail_receive_attachment` and `mail_receive_attachment_all`;
- `mail_change_sync`.

Do not treat planned names or generated templates as existing generic client send, remove, or batch-remove APIs.

## Attachments and Partial Commit

Mail attachments support only `DItemBasic` count data; an attachment carrying instance `data` is rejected as invalid.
Receipt checks precede item operations, but multi-container operations are not a global transaction: an intermediate
failure does not roll back earlier containers. Attachments already committed must be marked received and must not be
issued again through retries.

Successful receipt sets `EN_ITEM_CHG_MAJOR_MAIL_ATTACHMENT`, uses the mail `minor_type` as the minor source, and
stores `mail_id` in `reason_param`. See [item operations and change sources](item). `need_remove` defaults to true,
so even an empty mail or empty attachment operation is marked removed by the flow.

Rank settlement still has a placeholder entry in `rank_settlement_svr/service/settle_rank`; it does not automatically
deliver mail or items. Integrators must implement the actual reward flow.
