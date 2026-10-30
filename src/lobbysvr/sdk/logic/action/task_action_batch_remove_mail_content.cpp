// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#include "logic/action/task_action_batch_remove_mail_content.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/task_manager.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_context.h>

namespace atframework {
namespace mail {

LOBBY_RPC_API task_action_batch_remove_mail_content::task_action_batch_remove_mail_content(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {}

LOBBY_RPC_API task_action_batch_remove_mail_content::~task_action_batch_remove_mail_content() {}

LOBBY_RPC_API const char* task_action_batch_remove_mail_content::name() const {
  return "task_action_batch_remove_mail_content";
}

LOBBY_RPC_API task_action_batch_remove_mail_content::result_type task_action_batch_remove_mail_content::operator()() {
  for (auto& mail_id : param_.mail_ids) {
    int res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::remove_all(get_shared_context(), mail_id));
    if (0 != res && PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FCTXLOGWARNING(get_shared_context(), "Try to remove mail content {} failed, res: {}({})", mail_id, res,
                     protobuf_mini_dumper_get_error_msg(res));
    } else {
      FCTXLOGINFO(get_shared_context(), "Try to remove mail content {} success", mail_id);
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

LOBBY_RPC_API int task_action_batch_remove_mail_content::on_success() { return get_result(); }

LOBBY_RPC_API int task_action_batch_remove_mail_content::on_failed() { return get_result(); }

LOBBY_RPC_API int task_action_batch_remove_mail_content::create_and_start(rpc::context& ctx,
                                                                          std::vector<int64_t>&& mail_ids) {
  task_type_trait::task_type task_inst;
  task_action_batch_remove_mail_content::ctor_param_t params;
  params.mail_ids.swap(mail_ids);
  task_manager::me()->create_task_with_timeout<task_action_batch_remove_mail_content>(
      task_inst, logic_config::me()->get_cfg_task().nomsg().timeout(), std::move(params));
  if (task_type_trait::empty(task_inst)) {
    FCTXLOGERROR(ctx, "create task_action_batch_remove_mail_content failed");
  } else {
    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

    int res = task_manager::me()->start_task(task_inst, start_data);
    if (res < 0) {
      FCTXLOGERROR(ctx, "start task_action_batch_remove_mail_content failed, res: {}", res);
    }
    return res;
  }

  return 0;
}
}  // namespace mail
}  // namespace atframework
