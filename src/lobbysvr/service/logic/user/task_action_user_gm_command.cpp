// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-12 14:10:41

#include "logic/user/task_action_user_gm_command.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/user.h>
#include <dispatcher/task_manager.h>
#include <logic/user/task_action_user_gm_cmd_nomsg.h>
#include <rpc/rpc_async_invoke.h>

#include <unordered_map>

GAMECLIENT_SERVICE_API task_action_user_gm_command::task_action_user_gm_command(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_user_gm_command::~task_action_user_gm_command() {}

GAMECLIENT_SERVICE_API const char* task_action_user_gm_command::name() const { return "task_action_user_gm_command"; }

GAMECLIENT_SERVICE_API task_action_user_gm_command::result_type task_action_user_gm_command::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (false == logic_config::me()->get_logic_cfg().gm().enable_gm_cmd()) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_GM_CMD_ACCESS_DENY);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 启动GM异步任务
  task_type_trait::task_type task_inst;
  task_action_user_gm_cmd_nomsg::ctor_param_t task_params;

  auto rsp_body_ptr = atfw::memory::stl::make_shared<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp>();
  task_params.user_inst = user_inst;
  task_params.rsp_body = rsp_body_ptr;
  task_params.args.reserve(static_cast<size_t>(req_body.cmd_args_size()));
  for (int i = 0; i < req_body.cmd_args_size(); ++i) {
    task_params.args.push_back(req_body.cmd_args(i));
  }
  task_params.caller_context = &get_shared_context();
  task_manager::me()->create_task<task_action_user_gm_cmd_nomsg>(task_inst, std::move(task_params));

  if (task_type_trait::empty(task_inst)) {
    FWLOGERROR("create {} failed", "task_action_user_gm_cmd_nomsg");
    rsp_body_ptr->set_result_code(::PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  } else {
    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
    task_manager::me()->start_task(task_inst, start_data);

    RPC_AWAIT_IGNORE_RESULT(rpc::wait_task(get_shared_context(), task_inst));
  }

  protobuf_copy_message(rsp_body, *rsp_body_ptr);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_user_gm_command::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_user_gm_command::on_failed() { return get_result(); }
