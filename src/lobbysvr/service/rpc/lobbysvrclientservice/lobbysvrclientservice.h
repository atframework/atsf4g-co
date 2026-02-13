// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.LobbysvrClientService, please don't edit it

#ifndef GENERATED_API_RPC_LOBBYSVRCLIENTSERVICE_LOBBYSVRCLIENTSERVICE_H
#define GENERATED_API_RPC_LOBBYSVRCLIENTSERVICE_LOBBYSVRCLIENTSERVICE_H

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "rpc/rpc_common_types.h"

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <string>

#ifndef GAMECLIENT_SERVICE_API
#  define GAMECLIENT_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class session;
namespace rpc {
class context;
namespace lobbysvrclientservice {

// ============ hello.LobbysvrClientService.player_dirty_chg_sync ============
/**
 * @brief send hello::SCPlayerDirtyChgSync for player_dirty_chg_sync to session
 * @brief Push dirty data
 * @param __ctx            RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param __body           Message body to send
 * @param __session        Client session
 * @return 0 or error code
 */
GAMECLIENT_SERVICE_API rpc::always_ready_code_type send_player_dirty_chg_sync(context& __ctx,
                                                                              hello::SCPlayerDirtyChgSync& __body,
                                                                              session& __session);

/**
 * @brief send hello::SCPlayerDirtyChgSync for player_dirty_chg_sync to session
 * @brief Push dirty data
 * @param __ctx            RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param __body           Message body to send
 * @param __session        Client session
 * @param server_sequence  Use specify server sequence
 * @return 0 or error code
 */
GAMECLIENT_SERVICE_API rpc::always_ready_code_type send_player_dirty_chg_sync(context& __ctx,
                                                                              hello::SCPlayerDirtyChgSync& __body,
                                                                              session& __session,
                                                                              uint64_t server_sequence);

/**
 * @brief broadcast hello::SCPlayerDirtyChgSync for player_dirty_chg_sync to gateway service
 * @brief Push dirty data
 * @param __ctx            RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param __body           Message body to send
 * @param service_id       Service ID
 * @return 0 or error code
 */
GAMECLIENT_SERVICE_API rpc::always_ready_code_type broadcast_player_dirty_chg_sync(context& __ctx,
                                                                                   hello::SCPlayerDirtyChgSync& __body,
                                                                                   uint64_t service_id);
}  // namespace lobbysvrclientservice
}  // namespace rpc

#endif
