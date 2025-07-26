// Copyright 2022 atframework
// @brief Created by owent on 2022-03-01 11:45:04

#include "task_action_participator_resolve_transaction.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>

#include <dispatcher/task_manager.h>

#include "rpc/transaction/transaction_api.h"
#include "transaction_participator_handle.h"

namespace atframework {
namespace distributed_system {

DISTRIBUTED_TRANSACTION_SDK_API
task_action_participator_resolve_transaction::task_action_participator_resolve_transaction(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {}

DISTRIBUTED_TRANSACTION_SDK_API
    task_action_participator_resolve_transaction::~task_action_participator_resolve_transaction() {}

DISTRIBUTED_TRANSACTION_SDK_API const char* task_action_participator_resolve_transaction::name() const {
  return "task_action_participator_resolve_transaction";
}

DISTRIBUTED_TRANSACTION_SDK_API task_action_participator_resolve_transaction::result_type
task_action_participator_resolve_transaction::operator()() {
  // Maybe need to call
  // set_user_key(param_.user_id, param_.zone_id);

  if (!param_.participantor) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  do {
    bool is_writable = false;
    RPC_AWAIT_IGNORE_RESULT(param_.participantor->check_writable(get_shared_context(), is_writable));
    if (!is_writable) {
      break;
    }

    // 提交所有的已执行和已拒绝事务
    for (auto& trans_data : param_.submmit_transactions) {
      int32_t res = 0;
      const char* operation_name = "[NO RPC]";
      if (atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING ==
          trans_data->metadata().status()) {
        res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_participator(
            get_shared_context(), param_.participantor->get_participator_key(), *trans_data->mutable_metadata()));
        operation_name = "commit";
      } else if (atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING ==
                 trans_data->metadata().status()) {
        res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_participator(
            get_shared_context(), param_.participantor->get_participator_key(), *trans_data->mutable_metadata()));
        operation_name = "reject";
      }

      TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
      if (task_type_trait::is_exiting(current_task_status)) {
        FWLOGERROR("participator {} try to {} transaction {} failed, exiting. {}({})",
                   param_.participantor->get_participator_key(), operation_name,
                   trans_data->metadata().transaction_uuid(), res, protobuf_mini_dumper_get_error_msg(res));
        break;
      }

      // 不存在可能是之前已经执行过，这里是二次检查，直接视为成功即可
      if (res >= 0 || res == PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND ||
          res == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND ||
          res == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_NOT_FOUND) {
        RPC_AWAIT_IGNORE_RESULT(param_.participantor->remove_finished_transaction(get_shared_context(), trans_data));
        continue;
      }

      FWLOGERROR("participator {} commit transaction {} failed, res: {}({})",
                 param_.participantor->get_participator_key(), trans_data->metadata().transaction_uuid(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }

    // 检查所有的过期事务，准备resolve
    for (auto& trans_uuid : param_.pending_transactions) {
      is_writable = false;
      RPC_AWAIT_IGNORE_RESULT(param_.participantor->check_writable(get_shared_context(), is_writable));
      if (!is_writable) {
        break;
      }

      // 超出同步重试次数的直接移除
      int32_t res = RPC_AWAIT_CODE_RESULT(param_.participantor->resolve_transcation(get_shared_context(), trans_uuid));
      if (res < 0) {
        FWLOGERROR("participator {} resolve transaction {} failed, res: {}({})",
                   param_.participantor->get_participator_key(), trans_uuid, res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }
    // 重置下一次同步时间
    // 重置定时器
  } while (false);

  if (task_type_trait::get_task_id(param_.participantor->auto_resolve_transaction_task_) ==
      get_shared_context().get_task_context().task_id) {
    param_.participantor->auto_resolve_transaction_task_.reset();
  }

  auto& vtable = param_.participantor->vtable_;
  if (vtable && vtable->on_resolve_task_finished) {
    RPC_AWAIT_IGNORE_RESULT(vtable->on_resolve_task_finished(get_shared_context(), *param_.participantor));
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DISTRIBUTED_TRANSACTION_SDK_API int task_action_participator_resolve_transaction::on_success() {
  if (param_.participantor) {
    FWLOGINFO("participator {} do task_action_participator_resolve_transaction success",
              param_.participantor->get_participator_key());
  }

  return get_result();
}

DISTRIBUTED_TRANSACTION_SDK_API int task_action_participator_resolve_transaction::on_failed() {
  if (param_.participantor) {
    FWLOGINFO("participator {} do task_action_participator_resolve_transaction success",
              param_.participantor->get_participator_key());
  }
  return get_result();
}

}  // namespace distributed_system
}  // namespace atframework
