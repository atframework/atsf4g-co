---
title: Friend Services
---

# Friend Services

`src/friendsvr/` provides management and recommendation services with protocols and SDKs.
The lobby's `logic/friend_api/` integrates lists, invitations, acceptance/rejection, and removal.
Recommendation strategies remain unimplemented in `task_action_recommend_search` and the lobby's
`friend_get_suggest`; a successful call does not imply populated recommendations.
The lobby retains gift interfaces and local bookkeeping, but management transactions currently reject gift events,
so the flow is incomplete. Gift-item configuration, payload population, and granting also require business integration.

## Quick Start

1. Enable `friendsvr-management` in the values process layout, prepare Redis, and use
   `modules/friend_api.yaml` and the corresponding chart configuration.
   The management SDK depends on distributed transactions; prepare transaction services using
   [distributed transactions](distributed-transaction).
2. Add `friend-sdk-management` to the consumer's `USE_SERVICE_SDK` and reconfigure/build.
   The recommendation service framework supports custom strategies and is outside this minimal integration.
3. Follow `src/lobbysvr/service/logic/friend_api/user_friend_api_manager.h/.cpp` for business integration;
   use the SDK's `data/friend_cache.h` and `router/router_friend_manager.h` for data access.
4. Follow the lobby's `FriendManagementNotifyService` generation declaration and handler registration for notifications.
5. With two test users, send and accept an invitation, query both lists, then remove the relationship.
   Verify both users' data and notifications. Client messages are in `com.protocol.friend_api.proto`;
   robot's `cmd/friend.go` provides existing command entry points.

Adjust existing invitation and relationship count limits through configuration.
Follow the [RPC quick start](../development/add-rpc-task) for RPC declarations.

## Customization and Design

Management protocols live under `src/friendsvr/protocol/management/`, and recommendation protocols under
`protocol/recommend/`. Transaction participant and WAL implementations live under
`service/management/data/`; inspect them when changing relationship persistence or recovery.
Use `src/friendsvr/test/friendsvr_management_test.cpp` and lobby friend tests for validation.
