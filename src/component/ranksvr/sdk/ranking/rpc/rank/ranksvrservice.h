// Copyright 2026 atframework
// @brief Created by mako-generator.py for prx.RanksvrService, please don't edit it

#ifndef GENERATED_API_RPC_RANK_RANKSVRSERVICE_H
#define GENERATED_API_RPC_RANK_RANKSVRSERVICE_H

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rank_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp_config.h>

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

#include "rpc/rpc_common_types.h"
#include "dispatcher/dispatcher_type_defines.h"

#ifndef RANK_SDK_API
#  define RANK_SDK_API ATFW_UTIL_SYMBOL_VISIBLE
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
namespace rank {

// ============ prx.RanksvrService.rank_get_special ============
namespace packer {
RANK_SDK_API bool pack_rank_get_special(std::string& output, const prx::SSRankGetSpecifyRankReq& input);
RANK_SDK_API bool unpack_rank_get_special(const std::string& input, prx::SSRankGetSpecifyRankReq& output);
RANK_SDK_API bool pack_rank_get_special(std::string& output, const prx::SSRankGetSpecifyRankRsp& input);
RANK_SDK_API bool unpack_rank_get_special(const std::string& input, prx::SSRankGetSpecifyRankRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 获取指定排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_get_special(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankGetSpecifyRankReq &request_body, prx::SSRankGetSpecifyRankRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 获取指定排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_get_special(
    context& __ctx, uint64_t destination_server_id, prx::SSRankGetSpecifyRankReq &request_body, prx::SSRankGetSpecifyRankRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_set_score ============
namespace packer {
RANK_SDK_API bool pack_rank_set_score(std::string& output, const prx::SSRankSetScoreReq& input);
RANK_SDK_API bool unpack_rank_set_score(const std::string& input, prx::SSRankSetScoreReq& output);
RANK_SDK_API bool pack_rank_set_score(std::string& output, const prx::SSRankSetScoreRsp& input);
RANK_SDK_API bool unpack_rank_set_score(const std::string& input, prx::SSRankSetScoreRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 更新玩家排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_set_score(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankSetScoreReq &request_body, prx::SSRankSetScoreRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 更新玩家排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_set_score(
    context& __ctx, uint64_t destination_server_id, prx::SSRankSetScoreReq &request_body, prx::SSRankSetScoreRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_modify_score ============
namespace packer {
RANK_SDK_API bool pack_rank_modify_score(std::string& output, const prx::SSRankModifyScoreReq& input);
RANK_SDK_API bool unpack_rank_modify_score(const std::string& input, prx::SSRankModifyScoreReq& output);
RANK_SDK_API bool pack_rank_modify_score(std::string& output, const prx::SSRankModifyScoreRsp& input);
RANK_SDK_API bool unpack_rank_modify_score(const std::string& input, prx::SSRankModifyScoreRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 修改玩家排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_modify_score(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankModifyScoreReq &request_body, prx::SSRankModifyScoreRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 修改玩家排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_modify_score(
    context& __ctx, uint64_t destination_server_id, prx::SSRankModifyScoreReq &request_body, prx::SSRankModifyScoreRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_del_one_user ============
namespace packer {
RANK_SDK_API bool pack_rank_del_one_user(std::string& output, const prx::SSRankDelUserReq& input);
RANK_SDK_API bool unpack_rank_del_one_user(const std::string& input, prx::SSRankDelUserReq& output);
RANK_SDK_API bool pack_rank_del_one_user(std::string& output, const prx::SSRankDelUserRsp& input);
RANK_SDK_API bool unpack_rank_del_one_user(const std::string& input, prx::SSRankDelUserRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 删除单个玩家数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_del_one_user(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankDelUserReq &request_body, prx::SSRankDelUserRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 删除单个玩家数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_del_one_user(
    context& __ctx, uint64_t destination_server_id, prx::SSRankDelUserReq &request_body, prx::SSRankDelUserRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_get_top ============
namespace packer {
RANK_SDK_API bool pack_rank_get_top(std::string& output, const prx::SSRankGetTopReq& input);
RANK_SDK_API bool unpack_rank_get_top(const std::string& input, prx::SSRankGetTopReq& output);
RANK_SDK_API bool pack_rank_get_top(std::string& output, const prx::SSRankGetTopRsp& input);
RANK_SDK_API bool unpack_rank_get_top(const std::string& input, prx::SSRankGetTopRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 获取连续榜单数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_get_top(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankGetTopReq &request_body, prx::SSRankGetTopRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 获取连续榜单数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_get_top(
    context& __ctx, uint64_t destination_server_id, prx::SSRankGetTopReq &request_body, prx::SSRankGetTopRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_clear ============
namespace packer {
RANK_SDK_API bool pack_rank_clear(std::string& output, const prx::SSRankClearReq& input);
RANK_SDK_API bool unpack_rank_clear(const std::string& input, prx::SSRankClearReq& output);
RANK_SDK_API bool pack_rank_clear(std::string& output, const prx::SSRankClearRsp& input);
RANK_SDK_API bool unpack_rank_clear(const std::string& input, prx::SSRankClearRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 清理榜单
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_clear(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankClearReq &request_body, prx::SSRankClearRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 清理榜单
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_clear(
    context& __ctx, uint64_t destination_server_id, prx::SSRankClearReq &request_body, prx::SSRankClearRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_load_main ============
namespace packer {
RANK_SDK_API bool pack_rank_load_main(std::string& output, const prx::SSRankLoadMainReq& input);
RANK_SDK_API bool unpack_rank_load_main(const std::string& input, prx::SSRankLoadMainReq& output);
RANK_SDK_API bool pack_rank_load_main(std::string& output, const prx::SSRankLoadMainRsp& input);
RANK_SDK_API bool unpack_rank_load_main(const std::string& input, prx::SSRankLoadMainRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 主节点加载排行榜信息
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_load_main(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankLoadMainReq &request_body, prx::SSRankLoadMainRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 主节点加载排行榜信息
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_load_main(
    context& __ctx, uint64_t destination_server_id, prx::SSRankLoadMainReq &request_body, prx::SSRankLoadMainRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_check_slave ============
namespace packer {
RANK_SDK_API bool pack_rank_check_slave(std::string& output, const prx::SSRankCheckSlaveReq& input);
RANK_SDK_API bool unpack_rank_check_slave(const std::string& input, prx::SSRankCheckSlaveReq& output);
RANK_SDK_API bool pack_rank_check_slave(std::string& output, const prx::SSRankCheckSlaveRsp& input);
RANK_SDK_API bool unpack_rank_check_slave(const std::string& input, prx::SSRankCheckSlaveRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 查询是否是从节点并返回数据版本信息
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_check_slave(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankCheckSlaveReq &request_body, prx::SSRankCheckSlaveRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 查询是否是从节点并返回数据版本信息
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_check_slave(
    context& __ctx, uint64_t destination_server_id, prx::SSRankCheckSlaveReq &request_body, prx::SSRankCheckSlaveRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_event_sync ============
namespace packer {
RANK_SDK_API bool pack_rank_event_sync(std::string& output, const prx::SSRankEventSync& input);
RANK_SDK_API bool unpack_rank_event_sync(const std::string& input, prx::SSRankEventSync& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 主从节点信息同步
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::always_ready_code_type
  rank_event_sync(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankEventSync &request_body);
}  // namespace unicast

/**
 * @brief 主从节点信息同步
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::always_ready_code_type
  rank_event_sync(
    context& __ctx, uint64_t destination_server_id, prx::SSRankEventSync &request_body);

// ============ prx.RanksvrService.rank_heartbeat ============
namespace packer {
RANK_SDK_API bool pack_rank_heartbeat(std::string& output, const prx::SSRankHeartbeatReq& input);
RANK_SDK_API bool unpack_rank_heartbeat(const std::string& input, prx::SSRankHeartbeatReq& output);
RANK_SDK_API bool pack_rank_heartbeat(std::string& output, const prx::SSRankHeartbeatRsp& input);
RANK_SDK_API bool unpack_rank_heartbeat(const std::string& input, prx::SSRankHeartbeatRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 主从心跳
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_heartbeat(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankHeartbeatReq &request_body, prx::SSRankHeartbeatRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 主从心跳
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_heartbeat(
    context& __ctx, uint64_t destination_server_id, prx::SSRankHeartbeatReq &request_body, prx::SSRankHeartbeatRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_switch_to_slave ============
namespace packer {
RANK_SDK_API bool pack_rank_switch_to_slave(std::string& output, const prx::SSRankSwitchToSlaveReq& input);
RANK_SDK_API bool unpack_rank_switch_to_slave(const std::string& input, prx::SSRankSwitchToSlaveReq& output);
RANK_SDK_API bool pack_rank_switch_to_slave(std::string& output, const prx::SSRankSwitchToSlaveRsp& input);
RANK_SDK_API bool unpack_rank_switch_to_slave(const std::string& input, prx::SSRankSwitchToSlaveRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 切换到从节点
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_switch_to_slave(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankSwitchToSlaveReq &request_body, prx::SSRankSwitchToSlaveRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 切换到从节点
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_switch_to_slave(
    context& __ctx, uint64_t destination_server_id, prx::SSRankSwitchToSlaveReq &request_body, prx::SSRankSwitchToSlaveRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_get_special_one_front_back ============
namespace packer {
RANK_SDK_API bool pack_rank_get_special_one_front_back(std::string& output, const prx::SSRankGetUserFrontBackReq& input);
RANK_SDK_API bool unpack_rank_get_special_one_front_back(const std::string& input, prx::SSRankGetUserFrontBackReq& output);
RANK_SDK_API bool pack_rank_get_special_one_front_back(std::string& output, const prx::SSRankGetUserFrontBackRsp& input);
RANK_SDK_API bool unpack_rank_get_special_one_front_back(const std::string& input, prx::SSRankGetUserFrontBackRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 获取玩家前后排名count的排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_get_special_one_front_back(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankGetUserFrontBackReq &request_body, prx::SSRankGetUserFrontBackRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 获取玩家前后排名count的排行榜数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_get_special_one_front_back(
    context& __ctx, uint64_t destination_server_id, prx::SSRankGetUserFrontBackReq &request_body, prx::SSRankGetUserFrontBackRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_make_new_mirror ============
namespace packer {
RANK_SDK_API bool pack_rank_make_new_mirror(std::string& output, const prx::SSRankMakeNewMirrorReq& input);
RANK_SDK_API bool unpack_rank_make_new_mirror(const std::string& input, prx::SSRankMakeNewMirrorReq& output);
RANK_SDK_API bool pack_rank_make_new_mirror(std::string& output, const prx::SSRankMakeNewMirrorRsp& input);
RANK_SDK_API bool unpack_rank_make_new_mirror(const std::string& input, prx::SSRankMakeNewMirrorRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 请求生成排行榜镜像
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_make_new_mirror(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankMakeNewMirrorReq &request_body, prx::SSRankMakeNewMirrorRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 请求生成排行榜镜像
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_make_new_mirror(
    context& __ctx, uint64_t destination_server_id, prx::SSRankMakeNewMirrorReq &request_body, prx::SSRankMakeNewMirrorRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);

// ============ prx.RanksvrService.rank_check_mirror_dump_finish ============
namespace packer {
RANK_SDK_API bool pack_rank_check_mirror_dump_finish(std::string& output, const prx::SSRankCheckMirrorDumpFinishReq& input);
RANK_SDK_API bool unpack_rank_check_mirror_dump_finish(const std::string& input, prx::SSRankCheckMirrorDumpFinishReq& output);
RANK_SDK_API bool pack_rank_check_mirror_dump_finish(std::string& output, const prx::SSRankCheckMirrorDumpFinishRsp& input);
RANK_SDK_API bool unpack_rank_check_mirror_dump_finish(const std::string& input, prx::SSRankCheckMirrorDumpFinishRsp& output);
}  // namespace packer

namespace unicast {
/**
 * @brief 检查镜像是否生成完成
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_check_mirror_dump_finish(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankCheckMirrorDumpFinishReq &request_body, prx::SSRankCheckMirrorDumpFinishRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace unicast

/**
 * @brief 检查镜像是否生成完成
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param destination_server  target server
 * @param request_body       request body
 * @param response_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR RANK_SDK_API rpc::result_code_type
  rank_check_mirror_dump_finish(
    context& __ctx, uint64_t destination_server_id, prx::SSRankCheckMirrorDumpFinishReq &request_body, prx::SSRankCheckMirrorDumpFinishRsp &response_body, bool __no_wait = false, dispatcher_await_options* __wait_later = nullptr);
}  // namespace rank
}

#endif
