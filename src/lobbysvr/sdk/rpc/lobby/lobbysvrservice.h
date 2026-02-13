// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.LobbysvrService, please don't edit it

#ifndef GENERATED_API_RPC_LOBBY_LOBBYSVRSERVICE_H
#define GENERATED_API_RPC_LOBBY_LOBBYSVRSERVICE_H

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp_config.h>

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

#include "dispatcher/dispatcher_type_defines.h"
#include "rpc/rpc_common_types.h"

#ifndef GAME_RPC_API
#  define GAME_RPC_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

struct ss_msg_logic_index;

LIBATAPP_MACRO_NAMESPACE_BEGIN
class etcd_discovery_node;
LIBATAPP_MACRO_NAMESPACE_END

namespace atframework {
namespace atapp {
namespace protocol {
class atapp_metadata;
}  // namespace protocol
}  // namespace atapp
}  // namespace atframework

namespace rpc {
class context;
namespace lobby {

// ============ hello.LobbysvrService.player_kickoff ============
namespace packer {
GAME_RPC_API bool pack_player_kickoff(std::string& output, const hello::SSPlayerKickOffReq& input);
GAME_RPC_API bool unpack_player_kickoff(const std::string& input, hello::SSPlayerKickOffReq& output);
GAME_RPC_API bool pack_player_kickoff(std::string& output, const hello::SSPlayerKickOffRsp& input);
GAME_RPC_API bool unpack_player_kickoff(const std::string& input, hello::SSPlayerKickOffRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 通知提用户下线
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param destination_server  target server
 * @param zone_id             zone id that will be passsed into header
 * @param user_id             user id that will be passsed into header
 * @param open_id             open id that will be passsed into header
 * @param request_body       request body
 * @param response_body       response body
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR GAME_RPC_API rpc::result_code_type player_kickoff(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, uint32_t zone_id, uint64_t user_id,
    const std::string& open_id, hello::SSPlayerKickOffReq& request_body, hello::SSPlayerKickOffRsp& response_body);
}  // namespace unicast

/**
 * @brief 通知提用户下线
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param destination_server  target server
 * @param zone_id             zone id that will be passsed into header
 * @param user_id             user id that will be passsed into header
 * @param open_id             open id that will be passsed into header
 * @param request_body       request body
 * @param response_body       response body
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR GAME_RPC_API rpc::result_code_type player_kickoff(
    context& __ctx, uint64_t destination_server_id, uint32_t zone_id, uint64_t user_id, const std::string& open_id,
    hello::SSPlayerKickOffReq& request_body, hello::SSPlayerKickOffRsp& response_body);

// ============ hello.LobbysvrService.player_async_jobs_sync ============
namespace packer {
GAME_RPC_API bool pack_player_async_jobs_sync(std::string& output, const hello::SSPlayerAsyncJobsSync& input);
GAME_RPC_API bool unpack_player_async_jobs_sync(const std::string& input, hello::SSPlayerAsyncJobsSync& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 通知更新和执行异步任务
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param destination_server  target server
 * @param zone_id             zone id that will be passsed into header
 * @param user_id             user id that will be passsed into header
 * @param open_id             open id that will be passsed into header
 * @param request_body       request body
 * @note  notify another server instance to sync async jobs
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR GAME_RPC_API rpc::always_ready_code_type player_async_jobs_sync(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, uint32_t zone_id, uint64_t user_id,
    const std::string& open_id, hello::SSPlayerAsyncJobsSync& request_body);
}  // namespace unicast

/**
 * @brief 通知更新和执行异步任务
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on
 * stack
 * @param destination_server  target server
 * @param zone_id             zone id that will be passsed into header
 * @param user_id             user id that will be passsed into header
 * @param open_id             open id that will be passsed into header
 * @param request_body       request body
 * @note  notify another server instance to sync async jobs
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR GAME_RPC_API rpc::always_ready_code_type player_async_jobs_sync(
    context& __ctx, uint64_t destination_server_id, uint32_t zone_id, uint64_t user_id, const std::string& open_id,
    hello::SSPlayerAsyncJobsSync& request_body);
}  // namespace lobby
}  // namespace rpc

#endif
