// Copyright 2022 atframework
// @brief Created by owent on 2022-03-01 11:45:04

#include "logic/action/task_action_participator_resolve_transaction.h"

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

#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

#include <dispatcher/task_manager.h>

#include "rpc/transaction/transaction_api.h"
#include "transaction_participator_handle.h"  // NOLINT(build/include_subdir)

namespace atframework {
namespace distributed_system {

DISTRIBUTED_TRANSACTION_SDK_API
task_action_participator_resolve_transaction::task_action_participator_resolve_transaction(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param), pending_iter_(param_.pending_transactions.begin()) {}

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

  // tick 的 trigger_due 在拉起本任务前已移除所有待处理条目的 timer，恢复所有权随任务转移。
  // 已处理条目由 resolve_transcation/handle_finished_transaction_result 自行重新排期或完成清理；
  // 未处理条目由 rearm_unprocessed_timers 按重试次数清理或重新排期。
  // 进度状态保存在任务对象成员上，供 operator() 收尾与 on_failed 兜底共用 rearm_unprocessed_timers。
  do {
    bool is_writable = false;
    auto writable_result = param_.participantor->check_writable(get_shared_context(), is_writable);
    if (writable_result < 0 || !is_writable) {
      writable_check_failed_ = true;
      break;
    }

    // 提交所有的已执行和已拒绝事务
    // 任务进入退出状态后不再处理后续条目：退出中的 RPC 必然失败，只会增加各事务当前阶段的重试计数
    bool task_exiting = false;
    for (; submmit_processed_ < param_.submmit_transactions.size();
         param_.submmit_transactions.at(submmit_processed_).reset(), ++submmit_processed_) {
      // 首条沿用批次入口检查；上一条回调或 ACK 切出后，下一条须重新确认可写性。
      if (submmit_processed_ > 0) {
        is_writable = false;
        writable_result = param_.participantor->check_writable(get_shared_context(), is_writable);
        if (writable_result < 0 || !is_writable) {
          writable_check_failed_ = true;
          break;
        }
      }
      auto& trans_data = param_.submmit_transactions.at(submmit_processed_);
      if (!param_.participantor->is_current_transaction(
              transaction_participator_handle::resolve_timer_action_type::kAcknowledge, trans_data)) {
        continue;
      }
      int32_t res = 0;
      if (!trans_data->finished_callback_completed()) {
        res = RPC_AWAIT_CODE_RESULT(param_.participantor->start_finished_transaction(get_shared_context(), trans_data));
        if (res < 0 || !param_.participantor->is_current_transaction(
                           transaction_participator_handle::resolve_timer_action_type::kAcknowledge, trans_data)) {
          continue;
        }
      }
      const char* operation_name = "[NO RPC]";
      // 成功并校验对象身份后才合并响应，避免部分副本或旧任务的响应提前改写状态。
      auto acknowledge_metadata = rpc::make_shared_message<transaction_metadata>(get_shared_context());
      protobuf_copy_message(*acknowledge_metadata, trans_data->metadata());
      // 单次调用，失败由外层 acknowledge timer 到期后重新拉起整个 task 重试
      if (atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING == trans_data->metadata().status() ||
          atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED == trans_data->metadata().status()) {
        res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_participator(
            get_shared_context(), param_.participantor->get_participator_key(), *acknowledge_metadata));
        operation_name = "commit";
      } else if (atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING ==
                     trans_data->metadata().status() ||
                 atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED ==
                     trans_data->metadata().status()) {
        res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_participator(
            get_shared_context(), param_.participantor->get_participator_key(), *acknowledge_metadata));
        operation_name = "reject";
      } else {
        res = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      }

      TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
      if (task_type_trait::is_exiting(current_task_status)) {
        FWLOGERROR("participator {} try to {} transaction {} failed, exiting. {}({})",
                   param_.participantor->get_participator_key(), operation_name,
                   trans_data->metadata().transaction_uuid(), res, protobuf_mini_dumper_get_error_msg(res));
        task_exiting = true;
        break;
      }

      RPC_AWAIT_IGNORE_RESULT(param_.participantor->handle_finished_transaction_result(get_shared_context(), trans_data,
                                                                                       res, *acknowledge_metadata));
      if (res < 0) {
        FWLOGERROR("participator {} {} transaction {} failed, res: {}({})",
                   param_.participantor->get_participator_key(), operation_name,
                   trans_data->metadata().transaction_uuid(), res, protobuf_mini_dumper_get_error_msg(res));
      }
    }

    // 检查所有的过期事务，准备resolve
    // 每个已处理条目立即释放；后续事务的 IO/回调等待不能延长它的生命周期。
    for (; !task_exiting && !writable_check_failed_ && pending_iter_ != param_.pending_transactions.end();
         pending_iter_ = param_.pending_transactions.erase(pending_iter_)) {
      is_writable = false;
      writable_result = param_.participantor->check_writable(get_shared_context(), is_writable);
      if (writable_result < 0 || !is_writable) {
        writable_check_failed_ = true;
        break;
      }

      const auto& storage = *pending_iter_;
      if (!param_.participantor->is_current_transaction(
              transaction_participator_handle::resolve_timer_action_type::kQuery, storage)) {
        continue;
      }
      // 超出同步重试次数的直接移除
      int32_t res = RPC_AWAIT_CODE_RESULT(
          param_.participantor->resolve_transcation(get_shared_context(), storage->metadata().transaction_uuid()));
      if (res < 0) {
        FWLOGERROR("participator {} resolve transaction {} failed, res: {}({})",
                   param_.participantor->get_participator_key(), storage->metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
      TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
      if (task_type_trait::is_exiting(current_task_status)) {
        task_exiting = true;
        break;
      }
    }
    // 重置下一次同步时间
    // 重置定时器
  } while (false);

  // 正常收尾（含不可写/exiting 提前退出）：为未处理条目重新排期定时器
  rearm_unprocessed_timers();

  RPC_AWAIT_IGNORE_RESULT(param_.participantor->finish_resolve_task(get_shared_context()));

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
    FWLOGINFO("participator {} do task_action_participator_resolve_transaction failed, res: {}({})",
              param_.participantor->get_participator_key(), get_result(),
              protobuf_mini_dumper_get_error_msg(get_result()));
    // 任务被 kill/超时/取消时的兜底防线。libcopp 的 kill/cancel 仅置状态位、不销毁协程帧，
    // 且 task manager 会把 await 超时钳制在任务剩余生命周期内，on_failed 总是在 operator()
    // 收尾（rearm + 补驱动 tick）之后执行，因此正常构建下这里的两个调用都是空操作；
    // 仅在框架行为变化导致 operator() 收尾未执行时提供最后一层恢复保障。
    rearm_unprocessed_timers();
    param_.participantor->refresh_resolve_custom_timer();
  }
  return get_result();
}

void task_action_participator_resolve_transaction::rearm_unprocessed_timers() {
  if (rearm_done_ || !param_.participantor) {
    return;
  }
  rearm_done_ = true;

  // 按退避间隔重新排期：不重置时间戳会导致下一次 tick 立即再次拉起，失败原因持续存在时形成空转循环
  for (size_t i = submmit_processed_; i < param_.submmit_transactions.size(); ++i) {
    param_.participantor->retry_resolve_transaction(
        transaction_participator_handle::resolve_timer_action_type::kAcknowledge, param_.submmit_transactions.at(i),
        writable_check_failed_);
  }
  for (; pending_iter_ != param_.pending_transactions.end(); ++pending_iter_) {
    param_.participantor->retry_resolve_transaction(transaction_participator_handle::resolve_timer_action_type::kQuery,
                                                    *pending_iter_, writable_check_failed_);
  }
  // 剩余条目已清理或交还定时器；任务完成回调可能切出，不能继续持有这些 storage。
  param_.submmit_transactions.clear();
  param_.pending_transactions.clear();
  pending_iter_ = param_.pending_transactions.end();
}

}  // namespace distributed_system
}  // namespace atframework
