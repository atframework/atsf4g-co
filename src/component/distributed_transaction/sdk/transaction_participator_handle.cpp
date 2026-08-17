// Copyright 2022 atframework
// Created by owentou, on 2022-02-28

#include "transaction_participator_handle.h"  // NOLINT(build/include_subdir)

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#include <log/log_wrapper.h>

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <memory/object_allocator.h>

#include <dispatcher/task_manager.h>
#include <rpc/rpc_utils.h>

#include <algorithm>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rpc/transaction/transaction_api.h"

#include "logic/action/task_action_participator_resolve_transaction.h"

namespace atframework {
namespace distributed_system {

namespace {
static uint32_t get_retry_limit(const transaction_participator_storage& storage) {
  return std::max<uint32_t>(1, storage.configure().resolve_max_times());
}

// 每积累 16 个 finished 事务强制启动一次刷新操作（偏移量取 5，避免与定时器边界对齐）
constexpr size_t kFinishedTransactionAutoTickModulo = 16;
constexpr size_t kFinishedTransactionAutoTickPhase = 5;

static void set_next_retry_timepoint(transaction_participator_storage& storage,
                                     std::chrono::system_clock::time_point now) {
  auto retry_interval = protobuf_to_chrono_duration(storage.configure().resolve_retry_interval());
  if (retry_interval < std::chrono::system_clock::duration::zero()) {
    retry_interval = std::chrono::seconds(10);
  }
  protobuf_copy_message(*storage.mutable_resolve_timepoint(), protobuf_from_system_clock(now + retry_interval));
}

// 正在执行的最终状态方向标记的 RAII 守卫：
// 最终状态流程存在多条协程返回路径，手工复位一旦漏掉会永久卡死同事务的后续最终状态调用（同方向静默假成功），
// 统一交给析构复位，避免两条退出路径行为不一致；条目可能已被 remove_running_transaction 销毁，
// 析构时按 UUID 查找，找不到即为已完成清理
class inflight_terminal_mark_guard {
 public:
  inflight_terminal_mark_guard(
      std::unordered_map<std::string, transaction_participator_handle::running_transaction_entry>& owner,
      const std::string& transaction_uuid)
      : owner_(owner), transaction_uuid_(transaction_uuid) {}
  ~inflight_terminal_mark_guard() {
    auto iter = owner_.find(transaction_uuid_);
    if (iter != owner_.end()) {
      iter->second.inflight_terminal_direction = atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED;
    }
  }

  inflight_terminal_mark_guard(const inflight_terminal_mark_guard&) = delete;
  inflight_terminal_mark_guard& operator=(const inflight_terminal_mark_guard&) = delete;

 private:
  std::unordered_map<std::string, transaction_participator_handle::running_transaction_entry>& owner_;
  const std::string& transaction_uuid_;
};
}  // namespace

void transaction_participator_handle::resolve_timer_queue_type::insert_or_replace(resolve_timer_action_type action,
                                                                                  const storage_type& storage) {
  const std::string& transaction_uuid = storage.metadata().transaction_uuid();
  erase(transaction_uuid);
  storage_resolve_timer_type timer{action, storage};
  timers_.insert(timer);
  index_.insert_or_assign(transaction_uuid, timer);
}

void transaction_participator_handle::resolve_timer_queue_type::erase(const std::string& transaction_uuid) {
  auto iter = index_.find(transaction_uuid);
  if (iter == index_.end()) {
    return;
  }
  timers_.erase(iter->second);
  index_.erase(iter);
}

void transaction_participator_handle::resolve_timer_queue_type::erase(const storage_resolve_timer_type& timer) {
  auto iter = index_.find(timer.transaction_uuid);
  if (iter == index_.end() || iter->second != timer) {
    return;
  }
  timers_.erase(iter->second);
  index_.erase(iter);
}

void transaction_participator_handle::resolve_timer_queue_type::clear() {
  timers_.clear();
  index_.clear();
}

void transaction_participator_handle::schedule_resolve_retry(resolve_timer_action_type action, storage_type& storage) {
  set_next_retry_timepoint(storage, atfw::util::time::time_utility::now());
  resolve_timer_queue_.insert_or_replace(action, storage);
}

DISTRIBUTED_TRANSACTION_SDK_API void transaction_participator_handle::resolve_timer_queue_type::trigger_due(
    atfw::util::time::time_utility::raw_time_t timepoint,
    ::atfw::util::nostd::function_ref<bool(const storage_resolve_timer_type& timer)> fn) {
  while (!timers_.empty()) {
    auto iter = timers_.begin();
    if (iter->timepoint > timepoint) {
      break;
    }

    storage_resolve_timer_type timer_data = *iter;
    timers_.erase(iter);
    index_.erase(timer_data.transaction_uuid);

    if (!fn(timer_data)) {
      break;
    }
  }
}

DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle::transaction_participator_handle(
    const atfw::util::memory::strong_rc_ptr<vtable_type>& vtable, gsl::string_view participator_key)
    : private_data_{nullptr}, on_destroy_{nullptr}, vtable_{vtable} {
  participator_key_.assign(participator_key.data(), participator_key.size());
}

DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle::~transaction_participator_handle() {
  if (nullptr != on_destroy_) {
    (*on_destroy_)(this);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API void transaction_participator_handle::load(const snapshot_type& storage) {
  resolve_timer_queue_.clear();
  running_transactions_.clear();
  transaction_locks_.clear();
  finished_transactions_.clear();
  // 注意: 尚未结束的 auto resolve task 无法安全取消，但它只通过 resolve_transcation/handle_finished_transaction_result
  // 按 UUID 或对象标识操作当前容器，对 load 后的新状态是幂等安全的。

  for (const auto& transaction : storage.running_transaction()) {
    const std::string& transaction_uuid = transaction.metadata().transaction_uuid();
    if (transaction_uuid.empty()) {
      FWLOGERROR("participator {} load snapshot with empty transaction uuid, skip it", get_participator_key());
      continue;
    }
    if (running_transactions_.end() != running_transactions_.find(transaction_uuid)) {
      FWLOGERROR("participator {} load snapshot with duplicate running transaction {}, keep the first one",
                 get_participator_key(), transaction_uuid);
      continue;
    }

    auto transaction_ptr = atfw::component::memory::stl::make_strong_rc<storage_type>();
    if (!transaction_ptr) {
      FWLOGERROR("participator {} malloc transaction storage failed", get_participator_key());
      continue;
    }
    protobuf_copy_message(*transaction_ptr, transaction);
    auto& running_entry = running_transactions_[transaction_uuid];
    running_entry.storage = transaction_ptr;
    // A running REJECTING entry is a durable local rejection intent. This is also how a wound survives
    // snapshot reload, because the runtime-only wounded flag itself is not part of the snapshot schema.
    running_entry.wounded =
        transaction.metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING;

    // Restore locks
    for (const auto& lock_resource : transaction.lock_resource()) {
      auto lock_iter = transaction_locks_.find(lock_resource);
      if (lock_iter != transaction_locks_.end() && lock_iter->second &&
          lock_iter->second->metadata().transaction_uuid() != transaction_uuid) {
        FWLOGERROR(
            "participator {} load snapshot with conflicted lock resource {} of transaction {} (held by "
            "transaction {}), keep the first holder",
            get_participator_key(), lock_resource, transaction_uuid, lock_iter->second->metadata().transaction_uuid());
        continue;
      }
      transaction_locks_[lock_resource] = transaction_ptr;
    }

    resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kQuery, *transaction_ptr);
  }

  for (const auto& transaction : storage.finished_transaction()) {
    const std::string& transaction_uuid = transaction.metadata().transaction_uuid();
    if (transaction_uuid.empty()) {
      FWLOGERROR("participator {} load snapshot with empty finished transaction uuid, skip it", get_participator_key());
      continue;
    }
    if (running_transactions_.end() != running_transactions_.find(transaction_uuid) ||
        finished_transactions_.end() != finished_transactions_.find(transaction_uuid)) {
      FWLOGERROR("participator {} load snapshot with duplicate finished transaction {}, keep the first one",
                 get_participator_key(), transaction_uuid);
      continue;
    }

    auto transaction_ptr = atfw::component::memory::stl::make_strong_rc<storage_type>();
    if (!transaction_ptr) {
      FWLOGERROR("participator {} malloc transaction storage failed", get_participator_key());
      continue;
    }

    protobuf_copy_message(*transaction_ptr, transaction);
    finished_transactions_[transaction_uuid] = transaction_ptr;
    resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kAcknowledge, *transaction_ptr);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API void transaction_participator_handle::dump(snapshot_type& storage) {
  storage.Clear();

  for (auto& transaction : finished_transactions_) {
    if (transaction.second) {
      protobuf_copy_message(*storage.add_finished_transaction(), *transaction.second);
    }
  }

  for (auto& transaction : running_transactions_) {
    if (transaction.second.storage) {
      protobuf_copy_message(*storage.add_running_transaction(), *transaction.second.storage);
    }
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_participator_handle::check_writable(rpc::context& ctx,
                                                                                                      bool& writable) {
  if (!vtable_ || !vtable_->check_writable) {
    writable = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(vtable_->check_writable(ctx, *this, writable)));
}

DISTRIBUTED_TRANSACTION_SDK_API int32_t
transaction_participator_handle::tick(rpc::context&, atfw::util::time::time_utility::raw_time_t timepoint) {
  if (!task_type_trait::empty(auto_resolve_transaction_task_) &&
      !task_type_trait::is_exiting(auto_resolve_transaction_task_)) {
    return 0;
  }

  std::list<std::string> pending_transactions;
  std::vector<storage_ptr_type> submmit_transactions;

  // 统一队列按 action 分发：query 查 running 集合，acknowledge 查 finished 集合，均按截止时间处理。
  // trigger_due 在回调前移除 timer，无效条目（事务已不在对应集合）随之自动丢弃；
  // 有效条目的恢复所有权转交本函数：任务拉起失败时必须重新排期
  resolve_timer_queue_.trigger_due(timepoint, [&](const storage_resolve_timer_type& timer) {
    if (timer.action == resolve_timer_action_type::kQuery) {
      auto transaction_iter = running_transactions_.find(timer.transaction_uuid);
      if (transaction_iter == running_transactions_.end()) {
        return true;
      }

      if (!transaction_iter->second.storage) {
        running_transactions_.erase(transaction_iter);
        return true;
      }

      pending_transactions.push_back(transaction_iter->first);
    } else {
      auto transaction_iter = finished_transactions_.find(timer.transaction_uuid);
      if (transaction_iter == finished_transactions_.end() || !transaction_iter->second) {
        return true;
      }

      submmit_transactions.push_back(transaction_iter->second);
    }
    return true;
  });

  if (pending_transactions.empty() && submmit_transactions.empty()) {
    return 0;
  }

  // trigger_due 已移除这些条目的 timer，任务拉起失败时按退避间隔重新排期，
  // 避免恢复流程永久丢失或立即到期形成空转循环
  auto rearm_timers = [this, &pending_transactions, &submmit_transactions]() {
    for (const auto& transaction_uuid : pending_transactions) {
      auto transaction_iter = running_transactions_.find(transaction_uuid);
      if (transaction_iter != running_transactions_.end() && transaction_iter->second.storage) {
        schedule_resolve_retry(resolve_timer_action_type::kQuery, *transaction_iter->second.storage);
      }
    }
    for (const auto& transaction_ptr : submmit_transactions) {
      if (transaction_ptr) {
        schedule_resolve_retry(resolve_timer_action_type::kAcknowledge, *transaction_ptr);
      }
    }
  };

  task_action_participator_resolve_transaction::ctor_param_t params;
  params.participantor = shared_from_this();
  params.pending_transactions = pending_transactions;
  params.submmit_transactions = submmit_transactions;

  int32_t res = task_manager::me()->create_task<task_action_participator_resolve_transaction>(
      auto_resolve_transaction_task_, std::move(params));
  if (0 != res || task_type_trait::empty(auto_resolve_transaction_task_)) {
    FWLOGERROR("create task_action_participator_resolve_transaction failed, res: {}({})", res,
               protobuf_mini_dumper_get_error_msg(res));
    rearm_timers();
    return res;
  }

  dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
  res = task_manager::me()->start_task(task_type_trait::get_task_id(auto_resolve_transaction_task_), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_participator_resolve_transaction {} failed, res: {}({})",
               task_type_trait::get_task_id(auto_resolve_transaction_task_), res,
               protobuf_mini_dumper_get_error_msg(res));
    task_type_trait::reset_task(auto_resolve_transaction_task_);
    rearm_timers();
    return res;
  }

  return 0;
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_participator_handle::prepare(
    rpc::context& ctx, SSParticipatorTransactionPrepareReq&& request, SSParticipatorTransactionPrepareRsp& response,
    storage_ptr_type& output) {
  if (request.storage().metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/prepare"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.prepare", std::move(child_trace_option));

  if (vtable_ && vtable_->check_prepare) {
    rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(
        vtable_->check_prepare(child_ctx, *this, *request.mutable_storage(), *response.mutable_reason()));
    if (res < 0 || response.reason().allow_retry()) {
      // TODO(owentou): 通知被抢占的事务暂缓执行
      RPC_RETURN_CODE(child_tracer.finish({res, {}}));
    }
  }

  if (request.storage().configure().force_commit()) {
    FWLOGDEBUG("participator {} force commit transaction {}", get_participator_key(),
               request.storage().metadata().transaction_uuid());
    // Events
    rpc::result_code_type::value_type callback_result = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    if (vtable_ && vtable_->on_start_running) {
      callback_result = RPC_AWAIT_CODE_RESULT(vtable_->on_start_running(child_ctx, *this, request.storage()));
      if (callback_result < 0) {
        FWLOGERROR("participator {} call on_start_running for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), callback_result,
                   protobuf_mini_dumper_get_error_msg(callback_result));
      }
    }

    rpc::result_code_type::value_type event_result = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    if (vtable_ && vtable_->do_event) {
      event_result = RPC_AWAIT_CODE_RESULT(vtable_->do_event(child_ctx, *this, request.storage()));
      if (event_result < 0) {
        FWLOGERROR("participator {} call do_event for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), event_result,
                   protobuf_mini_dumper_get_error_msg(event_result));
      }
    }

    if (vtable_ && vtable_->on_finish_running) {
      callback_result = RPC_AWAIT_CODE_RESULT(vtable_->on_finish_running(child_ctx, *this, request.storage()));
      if (callback_result < 0) {
        FWLOGERROR("participator {} call on_finish_running for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), callback_result,
                   protobuf_mini_dumper_get_error_msg(callback_result));
      }
    }

    if (event_result < 0) {
      RPC_RETURN_CODE(child_tracer.finish({event_result, {}}));
    }

    if (vtable_ && vtable_->on_finished) {
      callback_result = RPC_AWAIT_CODE_RESULT(vtable_->on_finished(child_ctx, *this, request.storage()));
      if (callback_result < 0) {
        FWLOGERROR("participator {} call on_finished for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), callback_result,
                   protobuf_mini_dumper_get_error_msg(callback_result));
      }
    }

    if (vtable_ && vtable_->on_commited) {
      callback_result = RPC_AWAIT_CODE_RESULT(vtable_->on_commited(child_ctx, *this, request.storage()));
      if (callback_result < 0) {
        FWLOGERROR("participator {} call on_commited for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), callback_result,
                   protobuf_mini_dumper_get_error_msg(callback_result));
      }
    }
    RPC_RETURN_CODE(child_tracer.finish({event_result, {}}));
  } else {
    RPC_RETURN_CODE(child_tracer.finish(
        {RPC_AWAIT_CODE_RESULT(add_running_transcation(child_ctx, std::move(*request.mutable_storage()), output)),
         {}}));
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_participator_handle::commit(
    rpc::context& ctx, const SSParticipatorTransactionCommitReq& request, SSParticipatorTransactionCommitRsp&) {
  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/commit"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.commit", std::move(child_trace_option));

  // force_commit 时不会进入commit流程。prepare阶段就已经完成了。

  RPC_RETURN_CODE(
      child_tracer.finish({RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, request.transaction_uuid())), {}}));
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_participator_handle::reject(
    rpc::context& ctx, const SSParticipatorTransactionRejectReq& request, SSParticipatorTransactionRejectRsp&) {
  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/reject"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.reject", std::move(child_trace_option));

  if (request.has_storage() && request.storage().configure().force_commit()) {
    // 公开 RPC 可能收到畸形/伪造 payload，undo 前必须验证内外 UUID 一致
    if (!request.storage().metadata().transaction_uuid().empty() &&
        request.storage().metadata().transaction_uuid() != request.transaction_uuid()) {
      FWLOGERROR("participator {} undo transaction {} but request uuid {} mismatch", get_participator_key(),
                 request.storage().metadata().transaction_uuid(), request.transaction_uuid());
      RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, {}}));
    }

    FWLOGDEBUG("participator {} undo transaction {}", get_participator_key(),
               request.storage().metadata().transaction_uuid());

    rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    if (vtable_ && vtable_->undo_event) {
      ret = RPC_AWAIT_CODE_RESULT(vtable_->undo_event(child_ctx, *this, request.storage()));
      if (ret < 0) {
        FWLOGERROR("participator {} call undo_event for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), ret,
                   protobuf_mini_dumper_get_error_msg(ret));
      }
    }

    RPC_RETURN_CODE(child_tracer.finish({ret, {}}));
  }

  RPC_RETURN_CODE(
      child_tracer.finish({RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, request.transaction_uuid())), {}}));
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type::value_type transaction_participator_handle::check_lock(
    const transaction_metadata& metadata, gsl::span<const std::string> resource_uuids,
    std::list<storage_const_ptr_type>& preemption_transaction) {
  if (metadata.transaction_uuid().empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (metadata.status() >= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED;
  }

  preemption_transaction.clear();
  for (const auto& resource_uuid : resource_uuids) {
    do {
      auto old_holder = transaction_locks_.find(resource_uuid);
      if (old_holder == transaction_locks_.end()) {
        break;
      }
      if (!old_holder->second) {
        break;
      }

      bool is_preempted = false;
      if (old_holder->second->metadata().prepare_timepoint().seconds() != metadata.prepare_timepoint().seconds()) {
        is_preempted =
            old_holder->second->metadata().prepare_timepoint().seconds() < metadata.prepare_timepoint().seconds();
      } else if (old_holder->second->metadata().prepare_timepoint().seconds() ==
                     metadata.prepare_timepoint().seconds() &&
                 old_holder->second->metadata().prepare_timepoint().nanos() != metadata.prepare_timepoint().nanos()) {
        is_preempted =
            old_holder->second->metadata().prepare_timepoint().nanos() < metadata.prepare_timepoint().nanos();
      } else {
        is_preempted = old_holder->second->metadata().transaction_uuid() < metadata.transaction_uuid();
      }
      if (is_preempted) {
        preemption_transaction.push_back(util::memory::const_pointer_cast<const storage_type>(old_holder->second));
      }
    } while (false);
  }

  if (!preemption_transaction.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_participator_handle::lock(
    const storage_ptr_type& transaction_ptr, const google::protobuf::RepeatedPtrField<std::string>& resource_uuids) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (transaction_ptr->metadata().status() >= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  for (const auto& resource_uuid : resource_uuids) {
    // resource_uuids is already from lock_resource. there is no need to add again.
    if (&resource_uuids != &transaction_ptr->lock_resource()) {
      bool need_add_lock_resource = true;
      for (const auto& lock_uuid : transaction_ptr->lock_resource()) {
        if (lock_uuid == resource_uuid) {
          need_add_lock_resource = false;
          break;
        }
      }
      if (need_add_lock_resource) {
        transaction_ptr->add_lock_resource(resource_uuid);
      }
    }

    auto old_holder = transaction_locks_.find(resource_uuid);
    if (old_holder != transaction_locks_.end() && old_holder->second) {
      if (old_holder->second != transaction_ptr) {
        // Wound-Wait: 标记被抢占的事务，其资源所有权已不再完整，禁止后续 commit
        auto wounded_iter = running_transactions_.find(old_holder->second->metadata().transaction_uuid());
        if (wounded_iter != running_transactions_.end()) {
          wounded_iter->second.wounded = true;
          old_holder->second->mutable_metadata()->set_status(
              atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING);
          // Persist the local rejection intent and ensure the resolve timer cannot be consumed without
          // eventually moving the wounded transaction out of the running set.
          schedule_resolve_retry(resolve_timer_action_type::kQuery, *old_holder->second);
        } else {
          FWLOGERROR("participator {} wound transaction {} but it is not in running set", get_participator_key(),
                     old_holder->second->metadata().transaction_uuid());
        }
      }
      unlock(old_holder->second, resource_uuid);
    }
    transaction_locks_[resource_uuid] = transaction_ptr;
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DISTRIBUTED_TRANSACTION_SDK_API bool transaction_participator_handle::unlock(
    const storage_ptr_type& transaction_ptr, const std::string& resource_uuid) noexcept {
  if (!transaction_ptr) {
    return false;
  }

  auto lock_iter = transaction_locks_.find(resource_uuid);
  if (lock_iter == transaction_locks_.end()) {
    return false;
  }

  if (lock_iter->second && lock_iter->second != transaction_ptr) {
    return false;
  }

  protobuf_remove_repeated_if(*transaction_ptr->mutable_lock_resource(),
                              [&resource_uuid](const std::string& value) { return value == resource_uuid; });

  transaction_locks_.erase(lock_iter);
  return true;
}

DISTRIBUTED_TRANSACTION_SDK_API bool transaction_participator_handle::unlock(
    const std::string& transaction_uuid, const std::string& resource_uuid) noexcept {
  auto transaction_iter = running_transactions_.find(transaction_uuid);
  if (transaction_iter == running_transactions_.end()) {
    return false;
  }

  if (!transaction_iter->second.storage) {
    running_transactions_.erase(transaction_iter);
    return false;
  }

  return unlock(transaction_iter->second.storage, resource_uuid);
}

DISTRIBUTED_TRANSACTION_SDK_API bool transaction_participator_handle::unlock(
    const storage_ptr_type& transaction_ptr) noexcept {
  if (!transaction_ptr) {
    return false;
  }

  for (const auto& resource_uuid : transaction_ptr->lock_resource()) {
    auto lock_iter = transaction_locks_.find(resource_uuid);
    if (lock_iter == transaction_locks_.end()) {
      continue;
    }
    if (lock_iter->second && lock_iter->second != transaction_ptr) {
      continue;
    }
    transaction_locks_.erase(lock_iter);
  }
  transaction_ptr->clear_lock_resource();

  return true;
}

DISTRIBUTED_TRANSACTION_SDK_API bool transaction_participator_handle::unlock(
    const std::string& transaction_uuid) noexcept {
  auto transaction_iter = running_transactions_.find(transaction_uuid);
  if (transaction_iter == running_transactions_.end()) {
    return false;
  }

  if (!transaction_iter->second.storage) {
    running_transactions_.erase(transaction_iter);
    return false;
  }
  return unlock(transaction_iter->second.storage);
}

DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle::storage_ptr_type
transaction_participator_handle::get_locker(const std::string& resource) const noexcept {
  auto iter = transaction_locks_.find(resource);
  if (iter == transaction_locks_.end()) {
    return nullptr;
  }

  return iter->second;
}

DISTRIBUTED_TRANSACTION_SDK_API const
    std::unordered_map<std::string, transaction_participator_handle::running_transaction_entry>&
    transaction_participator_handle::get_running_transactions() const noexcept {
  return running_transactions_;
}

DISTRIBUTED_TRANSACTION_SDK_API const
    std::unordered_map<std::string, transaction_participator_handle::storage_ptr_type>&
    transaction_participator_handle::get_finished_transactions() const noexcept {
  return finished_transactions_;
}

rpc::result_code_type transaction_participator_handle::add_running_transcation(rpc::context& ctx,
                                                                               storage_type&& storage,
                                                                               storage_ptr_type& output) {
  if (storage.metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/add_running_transcation"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.add_running_transcation",
                         std::move(child_trace_option));

  const std::string transaction_uuid = storage.metadata().transaction_uuid();

  // 已完成的事务不允许重新进入 running，防止同一 UUID 再次执行 do_event
  auto finished_iter = finished_transactions_.find(transaction_uuid);
  if (finished_iter != finished_transactions_.end()) {
    FWLOGWARNING("participator {} prepare transaction {} but it is already finished, return the finished copy",
                 get_participator_key(), transaction_uuid);
    output = finished_iter->second;
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }

  // 重复 prepare 幂等返回现有 running 对象：不覆盖（可能正在执行 do_event 等最终状态动作）、
  // 不重建 timer/lock、不重复触发生命周期事件
  auto running_iter = running_transactions_.find(transaction_uuid);
  if (running_iter != running_transactions_.end() && running_iter->second.storage) {
    output = running_iter->second.storage;
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }

  // 同条目复用：storage 重建但运行时标记（wound/action-stage/inflight）保持原值
  auto& transaction_ptr = running_transactions_[transaction_uuid].storage;
  transaction_ptr = atfw::component::memory::stl::make_strong_rc<storage_type>();
  if (!transaction_ptr) {
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC, {}}));
  }
  output = transaction_ptr;

  protobuf_move_message(*output, std::move(storage));

  // Reset timer
  protobuf_copy_message(*output->mutable_resolve_timepoint(), output->metadata().expire_timepoint());
  resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kQuery, *output);

  // Auto lock resource
  if (!output->lock_resource().empty()) {
    RPC_AWAIT_IGNORE_RESULT(lock(output, output->lock_resource()));
  }

  FWLOGDEBUG("participator {} start run transaction {}", get_participator_key(), output->metadata().transaction_uuid());
  // event callback
  if (vtable_ && vtable_->on_start_running) {
    rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(vtable_->on_start_running(child_ctx, *this, *output));
    if (res < 0) {
      FWLOGERROR("participator {} call on_start_running for transaction {} failed, error code: {}({})",
                 get_participator_key(), output->metadata().transaction_uuid(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::remove_running_transaction(
    rpc::context& ctx, EnDistibutedTransactionStatus target_status, const std::string& transaction_uuid,
    storage_ptr_type* output) {
  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod,
       "atframework.transaction_participator_handle/remove_running_transaction"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.remove_running_transaction",
                         std::move(child_trace_option));

  if (target_status < atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, {}}));
  }

  auto iter = running_transactions_.find(transaction_uuid);
  if (iter == running_transactions_.end()) {
    FWLOGWARNING("participator {} try to remove transaction {} but not found", get_participator_key(),
                 transaction_uuid);
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }

  auto transaction_ptr = iter->second.storage;
  if (nullptr != output) {
    *output = transaction_ptr;
  }

  resolve_timer_queue_.erase(transaction_uuid);
  // 条目销毁即完成全部运行时标记（wound/action-stage/inflight）的生命周期清理
  running_transactions_.erase(iter);

  if (transaction_ptr) {
    FWLOGDEBUG("participator {} finish run transaction {}", get_participator_key(),
               transaction_ptr->metadata().transaction_uuid());
    // unlock resources
    unlock(transaction_ptr);

    // change status
    if (transaction_ptr->metadata().status() < atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
      transaction_ptr->mutable_metadata()->set_status(target_status);
    } else if (transaction_ptr->metadata().status() ==
                   atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING &&
               target_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
      transaction_ptr->mutable_metadata()->set_status(target_status);
    } else if (transaction_ptr->metadata().status() ==
                   atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING &&
               target_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
      transaction_ptr->mutable_metadata()->set_status(target_status);
    }

    // event callback
    if (vtable_ && vtable_->on_finish_running) {
      rpc::result_code_type::value_type res =
          RPC_AWAIT_CODE_RESULT(vtable_->on_finish_running(child_ctx, *this, *transaction_ptr));
      if (res < 0) {
        FWLOGERROR("participator {} call on_finish_running for transaction {} failed, error code: {}({})",
                   get_participator_key(), transaction_ptr->metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }
  }

  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::add_finished_transcation(
    rpc::context& ctx, const storage_ptr_type& transaction_ptr) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (transaction_ptr->metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod,
       "atframework.transaction_participator_handle/add_finished_transcation"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.add_finished_transcation",
                         std::move(child_trace_option));

  finished_transactions_[transaction_ptr->metadata().transaction_uuid()] = transaction_ptr;
  transaction_ptr->set_resolve_times(0);
  protobuf_copy_message(*transaction_ptr->mutable_resolve_timepoint(),
                        protobuf_from_system_clock(atfw::util::time::time_utility::now()));
  resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kAcknowledge, *transaction_ptr);

  FWLOGDEBUG("participator {} add finished transaction {}", get_participator_key(),
             transaction_ptr->metadata().transaction_uuid());

  // event callback
  if (vtable_ && vtable_->on_finished) {
    rpc::result_code_type::value_type res =
        RPC_AWAIT_CODE_RESULT(vtable_->on_finished(child_ctx, *this, *transaction_ptr));
    if (res < 0) {
      FWLOGERROR("participator {} call on_finished for transaction {} failed, error code: {}({})",
                 get_participator_key(), transaction_ptr->metadata().transaction_uuid(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::remove_finished_transaction(
    rpc::context& ctx, const storage_ptr_type& transaction_ptr) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto iter = finished_transactions_.find(transaction_ptr->metadata().transaction_uuid());
  if (iter == finished_transactions_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (iter->second != transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod,
       "atframework.transaction_participator_handle/remove_finished_transaction"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.remove_finished_transaction",
                         std::move(child_trace_option));

  FWLOGDEBUG("participator {} remove finished transaction {}", get_participator_key(),
             transaction_ptr->metadata().transaction_uuid());

  resolve_timer_queue_.erase(transaction_ptr->metadata().transaction_uuid());
  finished_transactions_.erase(iter);
  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::resolve_transcation(rpc::context& ctx,
                                                                           const std::string& transaction_uuid) {
  storage_ptr_type transaction_ptr;
  bool wounded = false;
  {
    auto running_iter = running_transactions_.find(transaction_uuid);
    if (running_iter != running_transactions_.end()) {
      transaction_ptr = running_iter->second.storage;
      wounded = running_iter->second.wounded;
      if (!transaction_ptr) {
        running_transactions_.erase(running_iter);
      }
    }
  }

  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_NOT_FOUND);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/resolve_transcation"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.resolve_transcation",
                         std::move(child_trace_option));

  if (wounded) {
    transaction_ptr->mutable_metadata()->set_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING);
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, transaction_uuid)), {}}));
  }

  if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED ||
      transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING) {
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, transaction_uuid)), {}}));
  }
  if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING) {
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, transaction_uuid)), {}}));
  }

  // Reset timer
  schedule_resolve_retry(resolve_timer_action_type::kQuery, *transaction_ptr);

  // retry too many times and reject it directly
  transaction_ptr->set_resolve_times(transaction_ptr->resolve_times() + 1);
  if (transaction_ptr->resolve_times() > get_retry_limit(*transaction_ptr)) {
    FWLOGERROR("participator {} resolve transaction {} for more than {} times, just reject it", get_participator_key(),
               transaction_uuid, get_retry_limit(*transaction_ptr));
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, transaction_uuid)), {}}));
  }

  rpc::context::message_holder<atfw::distributed_system::transaction_blob_storage> trans_data(child_ctx);
  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(
      rpc::transaction_api::query_transaction(child_ctx, transaction_ptr->metadata(), *trans_data));
  if (res == PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND || res == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_NOT_FOUND) {
    FWLOGWARNING("participator {} resolve transaction {} but not found, just remove it", get_participator_key(),
                 transaction_uuid);
    RPC_RETURN_CODE(
        child_tracer.finish({RPC_AWAIT_CODE_RESULT(remove_running_transaction(
                                 child_ctx, atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                                 transaction_uuid, &transaction_ptr)),
                             {}}));
  }

  if (res != 0) {
    FWLOGWARNING("participator {} resolve transaction {} failed, error code: {}({})", get_participator_key(),
                 transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
  }

  rpc::transaction_api::merge_storage(get_participator_key(), *transaction_ptr, *trans_data);

  // 进入本地最终状态动作阶段时不再重置 resolve_times：重试次数的重置只由 commit_transcation 的
  // local_action_stage_entered 首次进入逻辑负责。这里清零会让 timer 驱动的每次重入都拿到
  // 全新的重试次数，do_event 的失败重试变成无限重试（DT-020）。
  if (atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED == transaction_ptr->metadata().status()) {
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, transaction_uuid)), {}}));
  } else if (atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED ==
             transaction_ptr->metadata().status()) {
    transaction_ptr->mutable_metadata()->set_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING);
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, transaction_uuid)), {}}));
  }

  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::commit_transcation(rpc::context& ctx,
                                                                          const std::string& transaction_uuid) {
  // 如果移除了说明已经处理过了，不需要重复执行，这里支持并发
  auto iter = running_transactions_.find(transaction_uuid);
  if (iter == running_transactions_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto transaction_ptr = iter->second.storage;
  if (!transaction_ptr) {
    running_transactions_.erase(iter);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 同一事务的最终状态流程互斥：同一事务同一时刻只允许一个方向的流程在执行，
  // 防止 do_event 被重复执行或 commit/reject 方向竞态互相覆盖
  if (iter->second.inflight_terminal_direction != atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED) {
    if (iter->second.inflight_terminal_direction ==
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING) {
      // 同方向重复调用：正在执行的流程会完成相同动作，幂等返回
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    FWLOGERROR("participator {} commit transaction {} but a reject transition is in flight", get_participator_key(),
               transaction_uuid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  // Wound-Wait: 锁被抢占的事务资源所有权已不再完整，不允许 commit
  if (iter->second.wounded) {
    FWLOGERROR("participator {} commit transaction {} but its lock was preempted by an older transaction",
               get_participator_key(), transaction_uuid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/commit_transcation"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.commit_transcation",
                         std::move(child_trace_option));

  // 标记为执行中：此后跨协程边界不得再持有 iter，统一由 guard 析构复位
  iter->second.inflight_terminal_direction = atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING;
  inflight_terminal_mark_guard inflight_guard{running_transactions_, transaction_uuid};

  FWLOGINFO("participator {} commit transaction {}", get_participator_key(), transaction_uuid);
  rpc::result_code_type::value_type res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (transaction_ptr->metadata().status() != atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING &&
      vtable_ && vtable_->do_event) {
    // resolve_times 只在首次进入本地动作阶段时重置：此前 query 阶段的失败计数不应吃掉本地动作的重试次数，
    // 阶段内的重复进入（RPC 重发/timer）不再重置，保证动作重试次数在任意触发来源下都有限。
    // 协调者查询会先把状态推进到 COMMITED，因此不能用状态数值判断是否首次进入动作阶段。
    // （此处尚未发生协程切换，iter 仍然有效）
    if (!iter->second.local_action_stage_entered) {
      iter->second.local_action_stage_entered = true;
      transaction_ptr->set_resolve_times(0);
    }
    res = RPC_AWAIT_CODE_RESULT(vtable_->do_event(child_ctx, *this, *transaction_ptr));
    if (res < 0) {
      FWLOGERROR("participator {} call do_event for transaction {} failed, error code: {}({})", get_participator_key(),
                 transaction_ptr->metadata().transaction_uuid(), res, protobuf_mini_dumper_get_error_msg(res));
      transaction_ptr->set_resolve_times(transaction_ptr->resolve_times() + 1);
      if (transaction_ptr->resolve_times() < get_retry_limit(*transaction_ptr)) {
        schedule_resolve_retry(resolve_timer_action_type::kQuery, *transaction_ptr);
        RPC_RETURN_CODE(child_tracer.finish({res, {}}));
      }
      FWLOGERROR(
          "participator {} exhausted {} local commit attempts for transaction {}, consume by coordinator decision",
          get_participator_key(), get_retry_limit(*transaction_ptr), transaction_uuid);
    }
    transaction_ptr->set_resolve_times(0);
  }

  transaction_ptr->mutable_metadata()->set_status(
      atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);

  res = RPC_AWAIT_CODE_RESULT(
      remove_running_transaction(child_ctx, atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                                 transaction_uuid, &transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call remove running transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
  }
  res = RPC_AWAIT_CODE_RESULT(add_finished_transcation(child_ctx, transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call add finished transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
  }

  // event callback
  if (vtable_ && vtable_->on_commited) {
    res = RPC_AWAIT_CODE_RESULT(vtable_->on_commited(child_ctx, *this, *transaction_ptr));
    if (res < 0) {
      FWLOGERROR("participator {} call on_commited for transaction {} failed, error code: {}({})",
                 get_participator_key(), transaction_ptr->metadata().transaction_uuid(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  // 执行中标记由 guard 析构复位；remove_running_transaction 销毁条目时析构为空操作

  // 每16个事务强制启动一次刷新操作
  if (kFinishedTransactionAutoTickPhase == (finished_transactions_.size() % kFinishedTransactionAutoTickModulo)) {
    tick(child_ctx, atfw::util::time::time_utility::now());
  }

  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::reject_transcation(rpc::context& ctx,
                                                                          const std::string& transaction_uuid) {
  // 如果移除了说明已经处理过了，不需要重复执行，这里支持并发
  auto iter = running_transactions_.find(transaction_uuid);
  if (iter == running_transactions_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto transaction_ptr = iter->second.storage;
  if (!transaction_ptr) {
    running_transactions_.erase(iter);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 同一事务的最终状态流程互斥：同一事务同一时刻只允许一个方向的流程在执行
  if (iter->second.inflight_terminal_direction != atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED) {
    if (iter->second.inflight_terminal_direction ==
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING) {
      // 同方向重复调用：正在执行的流程会完成相同动作，幂等返回
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    FWLOGERROR("participator {} reject transaction {} but a commit transition is in flight", get_participator_key(),
               transaction_uuid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/reject_transcation"}};

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.reject_transcation",
                         std::move(child_trace_option));

  // 标记为执行中：此后跨协程边界不得再持有 iter，统一由 guard 析构复位
  iter->second.inflight_terminal_direction = atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING;
  inflight_terminal_mark_guard inflight_guard{running_transactions_, transaction_uuid};

  rpc::result_code_type::value_type res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  FWLOGINFO("participator {} reject transaction {}", get_participator_key(), transaction_uuid);

  res = RPC_AWAIT_CODE_RESULT(
      remove_running_transaction(child_ctx, atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                                 transaction_uuid, &transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call remove running transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
  }
  res = RPC_AWAIT_CODE_RESULT(add_finished_transcation(child_ctx, transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call add finished transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
  }

  // event callback
  if (vtable_ && vtable_->on_rejected) {
    res = RPC_AWAIT_CODE_RESULT(vtable_->on_rejected(child_ctx, *this, *transaction_ptr));
    if (res < 0) {
      FWLOGERROR("participator {} call on_rejected for transaction {} failed, error code: {}({})",
                 get_participator_key(), transaction_ptr->metadata().transaction_uuid(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  // 执行中标记由 guard 析构复位；remove_running_transaction 销毁条目时析构为空操作

  // 每16个事务强制启动一次刷新操作
  if (kFinishedTransactionAutoTickPhase == (finished_transactions_.size() % kFinishedTransactionAutoTickModulo)) {
    tick(child_ctx, atfw::util::time::time_utility::now());
  }
  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::handle_finished_transaction_result(
    rpc::context& ctx, const storage_ptr_type& transaction_ptr, int32_t result) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (result >= 0 || result == PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND ||
      result == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND ||
      result == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_NOT_FOUND) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove_finished_transaction(ctx, transaction_ptr)));
  }

  transaction_ptr->set_resolve_times(transaction_ptr->resolve_times() + 1);
  if (transaction_ptr->resolve_times() >= get_retry_limit(*transaction_ptr)) {
    FWLOGERROR("participator {} exhausted {} coordinator acknowledgement attempts for transaction {}, consume locally",
               get_participator_key(), get_retry_limit(*transaction_ptr),
               transaction_ptr->metadata().transaction_uuid());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove_finished_transaction(ctx, transaction_ptr)));
  }

  schedule_resolve_retry(resolve_timer_action_type::kAcknowledge, *transaction_ptr);
  RPC_RETURN_CODE(result);
}

}  // namespace distributed_system
}  // namespace atframework
