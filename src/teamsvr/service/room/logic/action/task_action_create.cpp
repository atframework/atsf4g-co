// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-20 21:15:33

#include "logic/action/task_action_create.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/rpc_context.h>
#include <rpc/team/team_common_api.h>

#include <utility>

#include "logic/room/team_room_manager.h"

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API task_action_create::task_action_create(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API task_action_create::~task_action_create() {}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API const char* task_action_create::name() const { return "task_action_create"; }

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API task_action_create::result_type task_action_create::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // 按队伍一致性哈希路由到 teamsvr-room 节点，不在本节点则转发
  uint64_t dest_server_id = rpc::team::team_api::get_teamsvr_room_server_id_of_zone(
      req_body.sender_user_key().zone_id(), req_body.team_key().team_id());
  if (0 == dest_server_id) {
    FCTXLOGERROR(get_shared_context(), "no ready teamsvr-room node for team {}", req_body.team_key().team_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (dest_server_id != logic_config::me()->get_local_server_id()) {
    bool forward_ok = false;
    auto forward_ret = RPC_AWAIT_CODE_RESULT(forward_rpc(dest_server_id, false, forward_ok));
    if (0 != forward_ret || !forward_ok) {
      FCTXLOGERROR(get_shared_context(), "forward create team {} to dest server {} failed! ret:{} ok:{}",
                   req_body.team_key().team_id(), dest_server_id, forward_ret, forward_ok ? 1 : 0);
      set_response_code(0 != forward_ret ? forward_ret : PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 本节点处理: 查找或创建房间(订阅频道并接管乐观锁)
  auto room = team_room_manager::me()->mutable_room(get_shared_context(), req_body.team_key().team_id());
  if (!room) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto ret = RPC_AWAIT_CODE_RESULT(room->await_ready(get_shared_context()));
  if (0 == ret) {
    // TODO: 创建队伍(写入初始快照与频道事件) ...
  }

  rsp_body.set_client_result(ret);
  if (ret < 0) {
    set_response_code(ret);
  }

  RPC_AWAIT_IGNORE_RESULT(room->flush_pending_channel_message(get_shared_context()));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API int task_action_create::on_success() { return get_result(); }

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API int task_action_create::on_failed() { return get_result(); }
