// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-10 20:11:30

#include "logic/action/task_action_heartbeat.h"

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

task_action_heartbeat::task_action_heartbeat(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

task_action_heartbeat::~task_action_heartbeat() {}

const char* task_action_heartbeat::name() const { return "task_action_heartbeat"; }

task_action_heartbeat::result_type task_action_heartbeat::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // 按队伍一致性哈希路由到 teamsvr-room 节点，不在本节点则转发
  uint64_t dest_server_id = rpc::team::team_api::get_teamsvr_room_server_id_of_zone(req_body.user_key().zone_id(),
                                                                                    req_body.team_key().team_id());
  if (0 == dest_server_id) {
    FCTXLOGERROR(get_shared_context(), "no ready teamsvr-room node for team {}", req_body.team_key().team_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (dest_server_id != logic_config::me()->get_local_server_id()) {
    bool forward_ok = false;
    auto forward_ret = RPC_AWAIT_CODE_RESULT(forward_rpc(dest_server_id, false, forward_ok));
    if (0 != forward_ret || !forward_ok) {
      FCTXLOGERROR(get_shared_context(), "forward team {} heartbeat to dest server {} failed! ret:{} ok:{}",
                   req_body.team_key().team_id(), dest_server_id, forward_ret, forward_ok ? 1 : 0);
      set_response_code(0 != forward_ret ? forward_ret : PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 本节点处理: 查找或创建房间(节点切换后首个心跳触发新节点订阅并接管)
  auto room = team_room_manager::me()->get_or_create_room(get_shared_context(), req_body.team_key().team_id());
  if (!room) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto ret = RPC_AWAIT_CODE_RESULT(room->await_ready(get_shared_context()));
  if (0 == ret) {
    ret = RPC_AWAIT_CODE_RESULT(room->heartbeat(get_shared_context(), req_body));
  }

  rsp_body.set_client_result(ret);
  if (ret < 0) {
    set_response_code(ret);
  }
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_heartbeat::on_success() { return get_result(); }

int task_action_heartbeat::on_failed() { return get_result(); }
