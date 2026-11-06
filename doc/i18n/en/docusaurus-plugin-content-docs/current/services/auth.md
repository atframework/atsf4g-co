---
title: Authentication and Account Management
---

# Authentication and Account Management

`authsvr` handles login authentication through `AuthsvrClientService`. Login records use the composite key
`(account_type, open_id)`. The channel is part of the account key, so another channel's record cannot be reused for
the same `open_id`.

## Internal Accounts

`internal_auth.enable` enables internal accounts, and `internal_auth.allow_register` controls new-account creation.
For an existing account, `last_account_data.access` is compared directly with the request's `access` as bytes. The
current implementation does not hash passwords; it is intended for controlled development and testing, not as a
production password-security design.

Pre-generate accounts with this GM command:

```text
auth-gen-accounts <count> <open-id prefix> [start index]
```

`count` must be from 1 to 10000, `open-id prefix` is required, and `start index` defaults to 1 and must be positive.
The account type is always internal. Existing accounts are not overwritten, and pre-generated accounts can log in
with `allow_register=false`.

The GM callback reports only whether the async task was scheduled, not whether every database write completed.
Generated `open_id` and password values are written to service logs in plaintext; restrict log access, retention, and
forwarding as sensitive data.

## Steam Offline Ticket Validation

Steam requires a build with the Steamworks SDK and `steam_auth.enable=true`. Only Encrypted App Ticket offline
validation is currently implemented:

- Decrypt with `rgub_key`, or with hexadecimal `rgub_key_hex` when `rgub_key` is empty.
- Validate `app_id`, ticket issue time, and that the SteamID matches `open_id`.
- Limit offline-ticket validity with `offline_token_timeout`, which defaults to 504 hours.

`enable_offline=false` does not switch to online validation; it causes Steam validation to fail. Although
`online_checking_interval` is declared, there is no online implementation or active periodic check. The online path
remains a TODO.

## Data Upgrade

Current login reads use the `(account_type, open_id)` composite key without an old-key fallback. Before upgrading old
data, plan the Redis-key and composite-key migration and coordinate Authsvr and consumer releases. See the
[data layer](../architecture/data-layer).

Implementation entry points are `src/authsvr/service/logic/algorithm/auth_validate.cpp`,
`src/authsvr/service/logic/algorithm/auth_account_generator.cpp`, and `src/authsvr/service/app/authsvr_main.cpp`.
