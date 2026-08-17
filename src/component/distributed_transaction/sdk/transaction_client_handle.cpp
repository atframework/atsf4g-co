// Copyright 2022 atframework
// Created by owentou, on 2022-03-03

#include "transaction_client_handle.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <time/time_utility.h>

#include <opentelemetry/semconv/incubating/rpc_attributes.h>
#include <utility/random_engine.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "rpc/transaction/transaction_api.h"

namespace atframework {
namespace distributed_system {
namespace {
// 协调者 CAS 冲突/故障转移/扩缩容时的重试次数上限
constexpr int32_t kCoordinatorRpcRetryTimes = 5;

inline bool is_retriable_create_error(int32_t res) {
  return res == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION || res == PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS;
}
}  // namespace

DISTRIBUTED_TRANSACTION_SDK_API transaction_client_handle::transaction_client_handle(
    const atfw::util::memory::strong_rc_ptr<vtable_type>& vtable)
    : private_data_{nullptr}, on_destroy_{nullptr}, vtable_{vtable} {}

DISTRIBUTED_TRANSACTION_SDK_API transaction_client_handle::~transaction_client_handle() {
  if (nullptr != on_destroy_) {
    (*on_destroy_)(this);
  }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_client_handle::create_transaction(
    rpc::context& ctx, storage_ptr_type& output, const transaction_options& options) {
  output = atfw::component::memory::stl::make_strong_rc<storage_type>();
  if (!output) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_client_handle/create_transaction"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_client_handle.create_transaction", std::move(child_trace_option));

  output->mutable_configure()->set_resolve_max_times(options.resolve_max_times);
  output->mutable_configure()->set_lock_retry_max_times(options.lock_retry_max_times);
  protobuf_copy_message(*output->mutable_configure()->mutable_resolve_retry_interval(),
                        protobuf_from_chrono_duration(options.resolve_retry_interval));
  protobuf_copy_message(*output->mutable_configure()->mutable_lock_wait_interval_min(),
                        protobuf_from_chrono_duration(options.lock_wait_interval_min));
  protobuf_copy_message(*output->mutable_configure()->mutable_lock_wait_interval_max(),
                        protobuf_from_chrono_duration(options.lock_wait_interval_max));

  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
      child_ctx, *output, options.timeout, options.replication_read_count, options.replication_total_count,
      options.memory_only, options.force_commit));
  if (res < 0) {
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
  }

  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_client_handle::submit_transaction(
    rpc::context& ctx, storage_ptr_type& input, std::unordered_set<std::string>* output_prepared_participators,
    std::unordered_set<std::string>* output_failed_participators) {
  if (!input) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (nullptr != output_prepared_participators) {
    output_prepared_participators->clear();
  }
  if (nullptr != output_failed_participators) {
    output_failed_participators->clear();
  }

  auto old_status = input->metadata().status();
  if (old_status > atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN);
  }
  if (input->participators().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  // 缺少当前模式所需 callback 时 prepare/最终状态通知会被静默跳过并造成假成功；force_commit 的成功路径
  // 在 prepare 内直接执行，不使用 commit_participator，但失败路径仍需要 reject_participator 做补偿。
  if (!vtable_ || !vtable_->prepare_participator || !vtable_->reject_participator ||
      (!input->configure().force_commit() && !vtable_->commit_participator)) {
    FWLOGERROR("transaction {} submit but required vtable callbacks are not fully set",
               input->metadata().transaction_uuid());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // An already-expired transaction must not create a coordinator record, commit or notify any
  // participant: the coordinator create would otherwise extend the expired deadline and resurrect
  // the transaction.
  auto expired_time = protobuf_to_system_clock(input->metadata().expire_timepoint());
  if (atfw::util::time::time_utility::now() >= expired_time) {
    FWLOGERROR("transaction {} submit but already expired", input->metadata().transaction_uuid());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_client_handle/submit_transaction"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_client_handle.submit_transaction", std::move(child_trace_option));

  input->mutable_metadata()->set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);

  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  // 创建事务,强制自动提交的事务不需要创建协调者事务对象
  // 协调者对相同内容的 create 幂等成功，CAS 冲突(OLD_VERSION/KEY_EXISTS)时做有限次重试，
  // 使晚到的 create 重放和落后的副本也有满足 R 的机会
  if (!input->configure().force_commit()) {
    for (int32_t left_retry_times = kCoordinatorRpcRetryTimes; left_retry_times-- > 0;) {
      ret = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(child_ctx, *input));
      if (ret >= 0 || !is_retriable_create_error(ret)) {
        break;
      }
    }
    if (ret < 0) {
      input->mutable_metadata()->set_status(old_status);
      RPC_RETURN_CODE(child_tracer.finish({ret, {}}));
    }
  }

  std::unordered_set<std::string> prepared_participators;
  if (nullptr == output_prepared_participators) {
    output_prepared_participators = &prepared_participators;
  }
  std::string failed_participator;
  // force_commit 下仅当 failed_participator 的 prepare 失败投递结果不确定时才补发一次 undo
  bool is_failed_participator_responded = false;
  bool prepare_complete = false;
  const uint64_t prepare_attempts = static_cast<uint64_t>(input->configure().lock_retry_max_times()) + 1;
  for (uint64_t retry_times = 0; retry_times < prepare_attempts; ++retry_times) {
    if (atfw::util::time::time_utility::now() >= expired_time) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
      break;
    }

    // 参与者准备
    bool retry_later = false;
    if (vtable_ && vtable_->prepare_participator) {
      for (const auto& participator : input->participators()) {
        // 如果已经成功过则重试的时候不用再执行prepare了
        if (output_prepared_participators->end() !=
            output_prepared_participators->find(participator.second.participator_key())) {
          continue;
        }
        transaction_participator_failure_reason failure_reason;
        ret = RPC_AWAIT_CODE_RESULT(
            vtable_->prepare_participator(child_ctx, *this, *input, participator.second, failure_reason));
        if ((ret >= 0 || ret == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) &&
            failure_reason.allow_retry()) {
          // 锁被占用则随机延迟重试
          retry_later = true;
          FWLOGWARNING("transaction {} prepare participator {} but resource preempted, we will retry soon later",
                       input->metadata().transaction_uuid(), participator.second.participator_key());

          if (ret == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) {
            failed_participator = participator.second.participator_key();
            is_failed_participator_responded = true;
          }
          break;
        }

        if (ret < 0) {
          FWLOGERROR("transaction {} prepare participator {} failed, error code: {}({})",
                     input->metadata().transaction_uuid(), participator.second.participator_key(), ret,
                     protobuf_mini_dumper_get_error_msg(ret));
          failed_participator = participator.second.participator_key();
          is_failed_participator_responded = false;
          break;
        }

        output_prepared_participators->insert(participator.second.participator_key());
        failed_participator.clear();
        is_failed_participator_responded = false;
      }
    }

    if (retry_later) {
      ret = PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED;
      if (retry_times + 1 >= prepare_attempts) {
        break;
      }

      auto delay_min = protobuf_to_chrono_duration(input->configure().lock_wait_interval_min());
      auto delay_max = protobuf_to_chrono_duration(input->configure().lock_wait_interval_max());

      if (delay_min >= delay_max) {
        ret = RPC_AWAIT_CODE_RESULT(rpc::wait(child_ctx, delay_min));
      } else {
        ret = RPC_AWAIT_CODE_RESULT(rpc::wait(
            child_ctx, std::chrono::system_clock::duration{
                           atfw::component::random_engine::fast_random_between(delay_min.count(), delay_max.count())}));
      }

      if (ret >= 0) {
        continue;
      }

      FWLOGERROR("transaction {} sleep and wait failed, error code: {}({})", input->metadata().transaction_uuid(), ret,
                 protobuf_mini_dumper_get_error_msg(ret));
      break;
    }

    if (ret < 0) {
      break;
    }

    prepare_complete = true;
    break;
  }

  if (!prepare_complete && ret >= 0) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  }
  const rpc::result_code_type::value_type prepare_result = ret;
  bool coordinator_decision_confirmed = input->configure().force_commit();

  if (input->configure().force_commit()) {
    input->mutable_metadata()->set_status(prepare_complete
                                              ? atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED
                                              : atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
  } else if (prepare_complete) {
    // 协调者的最终状态写入是幂等的；OLD_VERSION 说明缓存版本落后(故障转移/扩缩容)，有限次重试后可按幂等的最终状态返回
    for (int32_t left_retry_times = kCoordinatorRpcRetryTimes; left_retry_times-- > 0;) {
      ret = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(child_ctx, *input->mutable_metadata()));
      if (ret != PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
        break;
      }
    }
  } else {
    for (int32_t left_retry_times = kCoordinatorRpcRetryTimes; left_retry_times-- > 0;) {
      ret = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_transaction(child_ctx, *input->mutable_metadata()));
      if (ret != PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
        break;
      }
    }
  }

  if (!input->configure().force_commit() && ret >= 0 &&
      (input->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED ||
       input->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED)) {
    coordinator_decision_confirmed = true;
  }

  // Coordinator metadata is the only global decision. An RPC error may be returned after the decision is persisted,
  // so query it with a bounded retry budget before notifying any participant.
  if (!input->configure().force_commit() && !coordinator_decision_confirmed) {
    const uint32_t resolve_attempts = std::max<uint32_t>(1, input->configure().resolve_max_times());
    for (uint32_t retry_times = 0; retry_times < resolve_attempts; ++retry_times) {
      rpc::context::message_holder<storage_type> coordinator_storage(child_ctx);
      rpc::result_code_type::value_type query_result = RPC_AWAIT_CODE_RESULT(
          rpc::transaction_api::query_transaction(child_ctx, input->metadata(), *coordinator_storage));
      if (query_result >= 0) {
        rpc::transaction_api::merge_storage(*input, *coordinator_storage);
        if (input->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED ||
            input->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
          coordinator_decision_confirmed = true;
          ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
          break;
        }
      } else {
        ret = query_result;
      }

      if (retry_times + 1 < resolve_attempts) {
        rpc::result_code_type::value_type wait_result = RPC_AWAIT_CODE_RESULT(
            rpc::wait(child_ctx, protobuf_to_chrono_duration(input->configure().resolve_retry_interval())));
        if (wait_result < 0) {
          ret = wait_result;
          break;
        }
      }
    }
  }

  const auto coordinator_status = input->metadata().status();
  if (coordinator_decision_confirmed &&
      coordinator_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    FWLOGDEBUG("transaction {} commit success", input->metadata().transaction_uuid());
  } else if (coordinator_decision_confirmed &&
             coordinator_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
    ret = prepare_result < 0 ? prepare_result : PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED;
    FWLOGWARNING("transaction {} rejected by coordinator", input->metadata().transaction_uuid());
  } else {
    if (ret >= 0) {
      ret = prepare_result < 0 ? prepare_result : PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    }
    FWLOGERROR("transaction {} has no coordinator terminal decision, error code: {}({})",
               input->metadata().transaction_uuid(), ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  if (nullptr != output_failed_participators && !failed_participator.empty()) {
    output_failed_participators->insert(failed_participator);
  }

  const bool should_commit = coordinator_decision_confirmed &&
                             coordinator_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED;
  const bool should_reject = coordinator_decision_confirmed &&
                             coordinator_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  if ((should_commit || should_reject) && (!input->configure().force_commit() || should_reject) && vtable_) {
    const uint32_t notify_attempts = std::max<uint32_t>(1, input->configure().resolve_max_times());
    for (const auto& participator : input->participators()) {
      const std::string participator_key = participator.second.participator_key();
      const bool is_prepared =
          output_prepared_participators->find(participator_key) != output_prepared_participators->end();
      if (should_reject && !is_prepared) {
        // 未 prepare 的参与者不需要回滚；对投递结果不确定的 failed_participator 补发一次 undo/reject
        if (participator_key != failed_participator || is_failed_participator_responded) {
          continue;
        }
      }

      uint32_t participator_notify_attempts = notify_attempts;
      if (input->configure().force_commit() && should_reject && !is_prepared) {
        participator_notify_attempts = 1;
      }

      rpc::result_code_type::value_type notify_result = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
      for (uint32_t retry_times = 0; retry_times < participator_notify_attempts; ++retry_times) {
        if (should_commit && vtable_->commit_participator) {
          notify_result =
              RPC_AWAIT_CODE_RESULT(vtable_->commit_participator(child_ctx, *this, *input, participator.second));
        } else if (should_reject && vtable_->reject_participator) {
          notify_result =
              RPC_AWAIT_CODE_RESULT(vtable_->reject_participator(child_ctx, *this, *input, participator.second));
        } else {
          break;
        }

        if (notify_result >= 0) {
          break;
        }
        if (retry_times + 1 < participator_notify_attempts) {
          rpc::result_code_type::value_type wait_result = RPC_AWAIT_CODE_RESULT(
              rpc::wait(child_ctx, protobuf_to_chrono_duration(input->configure().resolve_retry_interval())));
          if (wait_result < 0) {
            notify_result = wait_result;
            break;
          }
        }
      }

      if (notify_result < 0) {
        // 最终状态通知投递失败由参与者的 resolve 重试流程保证最终一致，对 client 而言该参与者的事务执行
        // 视为成功，不进入 output_failed_participators。
        FWLOGWARNING("transaction {} notify {} participator {} failed after {} attempts, error code: {}({})",
                     input->metadata().transaction_uuid(), should_commit ? "commit" : "reject", participator_key,
                     participator_notify_attempts, notify_result, protobuf_mini_dumper_get_error_msg(notify_result));
      }
    }
  }
  RPC_RETURN_CODE(child_tracer.finish({ret, {}}));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
DISTRIBUTED_TRANSACTION_SDK_API int32_t transaction_client_handle::set_transaction_data(
    rpc::context&, storage_ptr_type& input, google::protobuf::Message& data) {
  if (!input) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (input->metadata().status() >= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN;
  }

  if (false == input->mutable_transaction_data()->PackFrom(data)) {
    FWLOGERROR("Pack transaction data from {} failed, message: {}", protobuf_mini_dumper_get_readable(data),
               input->transaction_data().InitializationErrorString());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
DISTRIBUTED_TRANSACTION_SDK_API int32_t transaction_client_handle::add_participator(rpc::context& ctx,
                                                                                    storage_ptr_type& input,
                                                                                    const std::string& participator_key,
                                                                                    google::protobuf::Message& data) {
  if (!input) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (input->metadata().status() >= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN;
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_client_handle/add_participator"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_client_handle.add_participator", std::move(child_trace_option));

  auto iter = input->mutable_participators()->find(participator_key);
  if (iter != input->mutable_participators()->end()) {
    if (false == iter->second.mutable_participator_data()->PackFrom(data)) {
      FWLOGERROR("Pack transaction participator data from {} failed, message: {}",
                 protobuf_mini_dumper_get_readable(data), iter->second.participator_data().InitializationErrorString());
      return child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PACK, {}});
    }
    return child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}});
  }

  // 先在临时对象上打包，成功后再插入，避免 PackFrom 失败在 map 中留下空条目
  atfw::distributed_system::transaction_participator participator;
  participator.set_participator_key(participator_key);
  participator.set_participator_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  if (false == participator.mutable_participator_data()->PackFrom(data)) {
    FWLOGERROR("Pack transaction participator data from {} failed, message: {}",
               protobuf_mini_dumper_get_readable(data), participator.participator_data().InitializationErrorString());
    return child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PACK, {}});
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  protobuf_move_message((*input->mutable_participators())[participator_key], std::move(participator));

  return child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}});
}

}  // namespace distributed_system
}  // namespace atframework
