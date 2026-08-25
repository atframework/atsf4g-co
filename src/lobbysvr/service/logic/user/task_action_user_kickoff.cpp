// Copyright 2021 atframework
// Created by owent with generate-for-pb.py at 2020-07-10 22:02:19
//

#include "logic/user/task_action_user_kickoff.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atgateway/protocol/libatgw_protocol_api.h>

#include <rpc/db/local_db_interface.atfw.gen.h>

#include <data/session.h>
#include <data/user.h>
#include <logic/session_manager.h>
#include <logic/user_manager.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/rpc_context.h>

#include <memory>
#include <string>

task_action_user_kickoff::task_action_user_kickoff(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}
task_action_user_kickoff::~task_action_user_kickoff() {}

bool task_action_user_kickoff::is_stream_rpc() const noexcept { return false; }

task_action_user_kickoff::result_type task_action_user_kickoff::operator()() {
  msg_cref_type req_msg = get_request();
  const rpc_request_type& req_body = get_request_body();

  uint64_t user_user_id = req_msg.head().user_user_id();
  uint32_t user_zone_id = req_msg.head().user_zone_id();
  const std::string user_open_id = req_msg.head().user_open_id();
  user::ptr_t user_inst = user_manager::me()->find_as<user>(user_user_id, user_zone_id);
  if (!user_inst) {
    FWLOGERROR("user {}({}:{}) not found, maybe already logout.", user_open_id, user_zone_id, user_user_id);

    // 尝试保存用户数据
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> user_lg{get_shared_context()};
    uint64_t version = 0;
    int res =
        RPC_AWAIT_CODE_RESULT(rpc::db::login_lock::get_all(get_shared_context(), user_user_id, *user_lg, version));
    if (res < 0) {
      FWLOGERROR("user {}({}:{}) try load login data failed.", user_open_id, user_zone_id, user_user_id);
      set_response_code(PROJECT_NAMESPACE_ID::err::EN_DB_REPLY_ERROR);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    if (user_lg->router_server_id() != logic_config::me()->get_local_server_id()) {
      FWLOGERROR("user {}({}:{}) login pd error(expected: 0x{:x}, real: 0x{:x})", user_open_id, user_zone_id,
                 user_user_id, logic_config::me()->get_local_server_id(), user_lg->router_server_id());
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    user_lg->set_router_server_id(0);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_lock::replace(get_shared_context(), user_lg, version));
    if (res < 0) {
      FWLOGERROR("user {}({}:{}) try load login data failed.", user_open_id, user_zone_id, user_user_id);
      set_response_code(PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  set_response_code(RPC_AWAIT_CODE_RESULT(user_inst->await_before_logout_tasks(get_shared_context())));
  if (get_response_code() < 0) {
    FWLOGERROR("{} kickoff failed, res: {}({})", *user_inst, get_response_code(),
               protobuf_mini_dumper_get_error_msg(get_response_code()));
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 仅在有session时才下发踢出消息
  std::shared_ptr<session> sess = user_inst->get_session();
  if (sess) {
    int32_t reason = static_cast<int32_t>(req_body.reason());
    if (reason == 0) {
      reason = static_cast<int32_t>(atfw::gateway::close_reason_t::kKickoff);
    }
    int32_t ret = sess->send_kickoff(
        reason, atfw::util::nostd::string_view{req_body.reason_message().data(), req_body.reason_message().size()});
    if (ret) {
      FWLOGERROR("task {} [{}] send cs msg failed, ret: {}", name(), get_task_id(), ret);

      // 发送失败也没有关系，下次客户端发包的时候自然会出错
    }
  }

  auto remove_res = RPC_AWAIT_CODE_RESULT(user_manager::me()->remove(get_shared_context(), user_inst, true));
  if (remove_res < 0) {
    FWLOGERROR("kickoff user {}({}:{}) failed, res: {}({})", user_inst->get_open_id(), user_zone_id,
               user_inst->get_user_id(), remove_res, protobuf_mini_dumper_get_error_msg(remove_res));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_user_kickoff::on_success() { return get_result(); }

int task_action_user_kickoff::on_failed() { return get_result(); }
