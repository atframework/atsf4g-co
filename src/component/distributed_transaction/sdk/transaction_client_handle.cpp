// Copyright 2022 atframework
// Created by owent, on 2022-03-03

#include "transaction_client_handle.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <time/time_utility.h>

#include <utility/random_engine.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

#include <unordered_set>

#include "rpc/transaction/transaction_api.h"

namespace atframework {
namespace distributed_system {

namespace {

template <class Rep, class Period>
static inline google::protobuf::Duration std_duration_to_protobuf_duration(std::chrono::duration<Rep, Period> input) {
  google::protobuf::Duration ret;
  ret.set_seconds(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(input).count()));
  ret.set_nanos(static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(input).count() % 1000000000));

  return ret;
}
}  // namespace

transaction_client_handle::transaction_client_handle(const std::shared_ptr<vtable_type>& vtable)
    : private_data_{nullptr}, on_destroy_{nullptr}, vtable_{vtable} {}

transaction_client_handle::~transaction_client_handle() {
  if (nullptr != on_destroy_) {
    (*on_destroy_)(this);
  }
}

rpc::result_code_type transaction_client_handle::create_transaction(rpc::context& ctx, storage_ptr_type& output,
                                                                    const transaction_options& options) {
  output = std::make_shared<storage_type>();
  if (!output) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_client_handle.create_transaction", std::move(child_trace_option));

  output->mutable_configure()->set_resolve_max_times(options.resolve_max_times);
  output->mutable_configure()->set_lock_retry_max_times(options.lock_retry_max_times);
  protobuf_copy_message(*output->mutable_configure()->mutable_resolve_retry_interval(),
                        std_duration_to_protobuf_duration(options.resolve_retry_interval));
  protobuf_copy_message(*output->mutable_configure()->mutable_lock_wait_interval_min(),
                        std_duration_to_protobuf_duration(options.lock_wait_interval_min));
  protobuf_copy_message(*output->mutable_configure()->mutable_lock_wait_interval_max(),
                        std_duration_to_protobuf_duration(options.lock_wait_interval_max));

  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
      child_ctx, *output, std_duration_to_protobuf_duration(options.timeout), options.replication_read_count,
      options.replication_total_count, options.memory_only, options.force_commit));
  if (res < 0) {
    RPC_RETURN_CODE(child_tracer.return_code(res));
  }

  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

rpc::result_code_type transaction_client_handle::submit_transaction(
    rpc::context& ctx, storage_ptr_type& input, std::unordered_set<std::string>* output_prepared_participators,
    std::unordered_set<std::string>* output_failed_participators) {
  if (!input) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto old_status = input->metadata().status();
  if (old_status > atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_client_handle.submit_transaction", std::move(child_trace_option));

  input->mutable_metadata()->set_status(atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);

  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  // 创建事务,强制自动提交的事务不需要创建协调者事务对象
  if (!input->configure().force_commit()) {
    ret = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(child_ctx, *input));
    if (ret < 0) {
      input->mutable_metadata()->set_status(old_status);
      RPC_RETURN_CODE(child_tracer.return_code(ret));
    }
  }

  auto expired_time = std::chrono::system_clock::from_time_t(input->metadata().expire_timepoint().seconds()) +
                      std::chrono::nanoseconds{input->metadata().expire_timepoint().nanos()};

  uint32_t retry_times = 0;
  std::unordered_set<std::string> prepared_participators;
  if (nullptr == output_prepared_participators) {
    output_prepared_participators = &prepared_participators;
  } else {
    output_prepared_participators->clear();
  }
  std::string failed_participator;
  for (auto now = util::time::time_utility::now();
       now < expired_time && retry_times <= input->configure().lock_retry_max_times();
       ++retry_times, now = util::time::time_utility::now()) {
    // 参与者准备
    bool retry_later = false;
    if (vtable_ && vtable_->prepare_participator) {
      for (auto& participator : input->participators()) {
        transaction_participator_failure_reason failure_reason;
        ret = RPC_AWAIT_CODE_RESULT(
            vtable_->prepare_participator(child_ctx, *this, *input, participator.second, failure_reason));
        if ((ret >= 0 || ret == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) &&
            failure_reason.allow_retry()) {
          // 锁被占用则随机延迟重试
          retry_later = true;
          FWLOGWARNING("transaction {} prepare participator {} but resource preempted, we will retry soon later",
                       input->metadata().transaction_uuid(), participator.second.participator_key());

          if (nullptr != output_failed_participators &&
              ret == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) {
            failed_participator = participator.second.participator_key();
          }
          break;
        } else if (ret < 0) {
          FWLOGERROR("transaction {} prepare participator {} failed, error code: {}({})",
                     input->metadata().transaction_uuid(), participator.second.participator_key(), ret,
                     protobuf_mini_dumper_get_error_msg(ret));

          if (nullptr != output_failed_participators) {
            failed_participator = participator.second.participator_key();
          }
          break;
        }

        output_prepared_participators->insert(participator.second.participator_key());
        failed_participator.clear();
      }
    }

    if (retry_later) {
      auto delay_min = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::seconds{input->configure().lock_wait_interval_min().seconds()} +
          std::chrono::nanoseconds{input->configure().lock_wait_interval_min().nanos()});
      auto delay_max = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::seconds{input->configure().lock_wait_interval_max().seconds()} +
          std::chrono::nanoseconds{input->configure().lock_wait_interval_max().nanos()});

      if (delay_min >= delay_max) {
        ret = RPC_AWAIT_CODE_RESULT(rpc::wait(child_ctx, delay_min));
      } else {
        ret = RPC_AWAIT_CODE_RESULT(
            rpc::wait(child_ctx, std::chrono::system_clock::duration{
                                     util::random_engine::fast_random_between(delay_min.count(), delay_max.count())}));
      }

      if (ret >= 0) {
        continue;
      } else {
        FWLOGERROR("transaction {} sleep and wait failed, error code: {}({})", input->metadata().transaction_uuid(),
                   ret, protobuf_mini_dumper_get_error_msg(ret));
        break;
      }
    } else if (ret < 0) {
      break;
    }

    // 提交事务,强制自动提交的事务不需要commit请求
    if (!input->configure().force_commit()) {
      ret = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(child_ctx, *input->mutable_metadata()));
    }
    break;
  }

  if (ret >= 0) {
    FWLOGDEBUG("transaction {} commit success", input->metadata().transaction_uuid());
    // 通知参与者成功提交
    // 自动强制提交的事务不需要通知参与者
    if (!input->configure().force_commit() && vtable_ && vtable_->commit_participator) {
      for (auto& participator : input->participators()) {
        ret = RPC_AWAIT_CODE_RESULT(vtable_->commit_participator(child_ctx, *this, *input, participator.second));
        if (ret < 0) {
          FWLOGERROR("transaction {} prepare participator {} failed, error code: {}({})",
                     input->metadata().transaction_uuid(), participator.second.participator_key(), ret,
                     protobuf_mini_dumper_get_error_msg(ret));
        }
      }
    }
  } else {
    FWLOGERROR("transaction {} commit failed, error code: {}({})", input->metadata().transaction_uuid(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
    // 通知参与者否决提交
    if (vtable_ && vtable_->reject_participator) {
      for (auto& participator_key : *output_prepared_participators) {
        auto iter = input->participators().find(participator_key);
        if (iter == input->participators().end()) {
          continue;
        }

        rpc::result_code_type::value_type res =
            RPC_AWAIT_CODE_RESULT(vtable_->reject_participator(child_ctx, *this, *input, iter->second));
        if (res < 0) {
          FWLOGERROR("transaction {} prepare participator {} failed, error code: {}({})",
                     input->metadata().transaction_uuid(), iter->second.participator_key(), res,
                     protobuf_mini_dumper_get_error_msg(res));
        }
      }
    }

    // 主动删除事务,因为可能参与角色没有全部有执行信息。如果resolve的时候发现不存在也会视为失败
    // 强制自动提交的事务不需要commit请求
    if (!input->configure().force_commit()) {
      rpc::result_code_type::value_type res =
          RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction_no_wait(child_ctx, input->metadata()));
      if (0 != res) {
        FWLOGERROR("transaction {} remove failed, res: {}({})", input->metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }

    if (nullptr != output_failed_participators && !failed_participator.empty()) {
      output_failed_participators->insert(std::move(failed_participator));
    }
  }
  RPC_RETURN_CODE(child_tracer.return_code(ret));
}

int32_t transaction_client_handle::set_transaction_data(rpc::context&, storage_ptr_type& input,
                                                        google::protobuf::Message& data) {
  if (!input) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (input->metadata().status() >= atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN;
  }

  if (false == input->mutable_transaction_data()->PackFrom(data)) {
    FWLOGERROR("Pack transaction data from {} failed, message: {}", protobuf_mini_dumper_get_readable(data),
               input->transaction_data().InitializationErrorString());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t transaction_client_handle::add_participator(rpc::context& ctx, storage_ptr_type& input,
                                                    const std::string& participator_key,
                                                    google::protobuf::Message& data) {
  if (!input) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (input->metadata().status() >= atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN;
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_client_handle.add_participator", std::move(child_trace_option));

  auto iter = input->mutable_participators()->find(participator_key);
  if (iter != input->mutable_participators()->end()) {
    if (false == iter->second.mutable_participator_data()->PackFrom(data)) {
      FWLOGERROR("Pack transaction participator data from {} failed, message: {}",
                 protobuf_mini_dumper_get_readable(data), iter->second.participator_data().InitializationErrorString());
    }
    return child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  auto& participator = (*input->mutable_participators())[participator_key];
  participator.set_participator_key(participator_key);
  participator.set_participator_status(atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  if (false == participator.mutable_participator_data()->PackFrom(data)) {
    FWLOGERROR("Pack transaction participator data from {} failed, message: {}",
               protobuf_mini_dumper_get_readable(data), participator.participator_data().InitializationErrorString());
    return child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  return child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace distributed_system
}  // namespace atframework
