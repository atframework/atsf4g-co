// Copyright 2022 atframework
// Created by owent, on 2022-02-28

#include "transaction_participator_handle.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <utility/protobuf_mini_dumper.h>

#include <log/log_wrapper.h>

#include <dispatcher/task_manager.h>

#include "rpc/transaction/transaction_api.h"

#include "logic/action/task_action_participator_resolve_transaction.h"

namespace atframework {
namespace distributed_system {

transaction_participator_handle::transaction_participator_handle(const std::shared_ptr<vtable_type>& vtable,
                                                                 gsl::string_view participator_key)
    : private_data_{nullptr}, on_destroy_{nullptr}, vtable_{vtable} {
  participator_key_.assign(participator_key.data(), participator_key.size());
}

transaction_participator_handle::~transaction_participator_handle() {
  if (nullptr != on_destroy_) {
    (*on_destroy_)(this);
  }
}

void transaction_participator_handle::load(const snapshot_type& storage) {
  resolve_timers_.clear();
  running_transactions_.clear();
  transaction_locks_.clear();
  finished_transactions_.clear();

  for (auto& transaction : storage.running_transaction()) {
    auto transaction_ptr = std::make_shared<storage_type>();
    if (!transaction_ptr) {
      FWLOGERROR("participator {} malloc transaction storage failed", get_participator_key());
      continue;
    }
    protobuf_copy_message(*transaction_ptr, transaction);
    running_transactions_[transaction.metadata().transaction_uuid()] = transaction_ptr;

    // Restore locks
    for (auto& lock_resource : transaction.lock_resource()) {
      transaction_locks_[lock_resource] = transaction_ptr;
    }

    resolve_timers_.insert(storage_resolve_timer_type{transaction});
  }

  for (auto& transaction : storage.finished_transaction()) {
    auto transaction_ptr = std::make_shared<storage_type>();
    if (!transaction_ptr) {
      FWLOGERROR("participator {} malloc transaction storage failed", get_participator_key());
      continue;
    }

    protobuf_copy_message(*transaction_ptr, transaction);
    finished_transactions_[transaction.metadata().transaction_uuid()] = transaction_ptr;
  }
}

void transaction_participator_handle::dump(snapshot_type& storage) {
  storage.Clear();

  for (auto& transaction : finished_transactions_) {
    if (transaction.second) {
      protobuf_copy_message(*storage.add_finished_transaction(), *transaction.second);
    }
  }

  for (auto& transaction : running_transactions_) {
    if (transaction.second) {
      protobuf_copy_message(*storage.add_running_transaction(), *transaction.second);
    }
  }
}

rpc::result_code_type transaction_participator_handle::check_writable(rpc::context& ctx, bool& writable) {
  if (!vtable_ || !vtable_->check_writable) {
    writable = true;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  return vtable_->check_writable(ctx, *this, writable);
}

int32_t transaction_participator_handle::tick(rpc::context& ctx, util::time::time_utility::raw_time_t timepoint) {
  if (auto_resolve_transaction_task_ && !auto_resolve_transaction_task_->is_exiting()) {
    return 0;
  }

  std::list<std::string> pending_transactions;
  std::vector<storage_ptr_type> submmit_transactions;

  std::list<storage_resolve_timer_type> invalid_timers;
  for (auto iter = resolve_timers_.begin(); iter != resolve_timers_.end(); ++iter) {
    // 强制同步定时器
    if ((*iter).timepoint >= timepoint) {
      break;
    }

    auto transaction_iter = running_transactions_.find((*iter).transaction_uuid);

    if (transaction_iter == running_transactions_.end()) {
      invalid_timers.push_back(*iter);
      continue;
    }

    if (!transaction_iter->second) {
      running_transactions_.erase(transaction_iter);
      invalid_timers.push_back(*iter);
      continue;
    }

    pending_transactions.push_back(transaction_iter->first);
  }

  // Cleanup dirty timers
  for (auto& invalid_timer : invalid_timers) {
    resolve_timers_.erase(invalid_timer);
  }

  // submit list
  submmit_transactions.reserve(finished_transactions_.size());
  for (auto& transaction : finished_transactions_) {
    if (transaction.second) {
      submmit_transactions.push_back(transaction.second);
    }
  }

  if (pending_transactions.empty() && submmit_transactions.empty()) {
    return 0;
  }

  task_action_participator_resolve_transaction::ctor_param_t params;
  params.participantor = shared_from_this();
  params.pending_transactions.swap(pending_transactions);
  params.submmit_transactions.swap(submmit_transactions);

  int32_t res = task_manager::me()->create_task<task_action_participator_resolve_transaction>(
      auto_resolve_transaction_task_, std::move(params));
  if (0 != res || !auto_resolve_transaction_task_) {
    FWLOGERROR("create task_action_friend_auto_resolve_transaction failed, res: {}({})", res,
               protobuf_mini_dumper_get_error_msg(res));
    return res;
  }

  task_manager::start_data_t start_data = dispatcher_make_default<dispatcher_start_data_t>();
  res = task_manager::me()->start_task(auto_resolve_transaction_task_->get_id(), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_participator_resolve_transaction {} failed, res: {}({})",
               auto_resolve_transaction_task_->get_id(), res, protobuf_mini_dumper_get_error_msg(res));
    auto_resolve_transaction_task_.reset();
    return res;
  }

  return 0;
}

rpc::result_code_type transaction_participator_handle::prepare(rpc::context& ctx,
                                                               SSParticipatorTransactionPrepareReq&& request,
                                                               SSParticipatorTransactionPrepareRsp& response,
                                                               storage_ptr_type& output) {
  if (request.storage().metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.prepare", std::move(child_trace_option));

  if (vtable_ && vtable_->check_prepare) {
    rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(
        vtable_->check_prepare(child_ctx, *this, *request.mutable_storage(), *response.mutable_reason()));
    if (res < 0) {
      // TODO(owent): 通知被抢占的事务暂缓执行
      RPC_RETURN_CODE(res);
    }
  }

  if (request.storage().configure().force_commit()) {
    FWLOGDEBUG("participator {} force commit transaction {}", get_participator_key(),
               request.storage().metadata().transaction_uuid());
    // Events
    rpc::result_code_type::value_type res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    if (vtable_ && vtable_->on_start_running) {
      res = RPC_AWAIT_CODE_RESULT(vtable_->on_start_running(child_ctx, *this, request.storage()));
      if (res < 0) {
        FWLOGERROR("participator {} call on_start_running for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }

    if (vtable_ && vtable_->do_event) {
      res = RPC_AWAIT_CODE_RESULT(vtable_->do_event(child_ctx, *this, request.storage()));
      if (res < 0) {
        FWLOGERROR("participator {} call do_event for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }

    if (vtable_ && vtable_->on_finish_running) {
      res = RPC_AWAIT_CODE_RESULT(vtable_->on_finish_running(child_ctx, *this, request.storage()));
      if (res < 0) {
        FWLOGERROR("participator {} call on_finish_running for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }

    if (vtable_ && vtable_->on_finished) {
      res = RPC_AWAIT_CODE_RESULT(vtable_->on_finished(child_ctx, *this, request.storage()));
      if (res < 0) {
        FWLOGERROR("participator {} call on_finished for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }

    if (vtable_ && vtable_->on_commited) {
      res = RPC_AWAIT_CODE_RESULT(vtable_->on_commited(child_ctx, *this, request.storage()));
      if (res < 0) {
        FWLOGERROR("participator {} call on_commited for transaction {} failed, error code: {}({})",
                   get_participator_key(), request.storage().metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }
    RPC_RETURN_CODE(child_tracer.return_code(res));
  } else {
    RPC_RETURN_CODE(child_tracer.return_code(
        RPC_AWAIT_CODE_RESULT(add_running_transcation(child_ctx, std::move(*request.mutable_storage()), output))));
  }
}

rpc::result_code_type transaction_participator_handle::commit(rpc::context& ctx,
                                                              const SSParticipatorTransactionCommitReq& request,
                                                              SSParticipatorTransactionCommitRsp& response) {
  return commit_transcation(ctx, request.transaction_uuid());
}

rpc::result_code_type transaction_participator_handle::reject(rpc::context& ctx,
                                                              const SSParticipatorTransactionRejectReq& request,
                                                              SSParticipatorTransactionRejectRsp& response) {
  if (request.has_storage() && request.storage().configure().force_commit()) {
    rpc::context child_ctx{ctx};
    rpc::context::tracer child_tracer;
    rpc::context::trace_option child_trace_option;
    child_trace_option.dispatcher = nullptr;
    child_trace_option.is_remote = false;
    child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

    child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.reject", std::move(child_trace_option));

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

    RPC_RETURN_CODE(ret);
  }

  return reject_transcation(ctx, request.transaction_uuid());
}

rpc::result_code_type::value_type transaction_participator_handle::check_lock(
    const transaction_metadata& metadata, gsl::span<const std::string> resource_uuids,
    std::list<storage_const_ptr_type>& preemption_transaction) {
  if (metadata.transaction_uuid().empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (metadata.status() >= atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED;
  }

  preemption_transaction.clear();
  for (auto& resource_uuid : resource_uuids) {
    do {
      auto old_holder = transaction_locks_.find(resource_uuid);
      if (old_holder == transaction_locks_.end()) {
        break;
      }
      if (!old_holder->second) {
        break;
      }

      // 已完成的事务可以忽略锁
      if (old_holder->second->metadata().status() >=
          atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
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
        preemption_transaction.push_back(std::const_pointer_cast<const storage_type>(old_holder->second));
      }
    } while (false);
  }

  if (!preemption_transaction.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

rpc::result_code_type transaction_participator_handle::lock(
    const storage_ptr_type& transaction_ptr, const google::protobuf::RepeatedPtrField<std::string>& resource_uuids) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (transaction_ptr->metadata().status() >=
      atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  bool from_empty = transaction_ptr->lock_resource().empty();
  for (auto& resource_uuid : resource_uuids) {
    // resource_uuids is already from lock_resource. there is no need to add again.
    if (&resource_uuids != &transaction_ptr->lock_resource()) {
      bool need_add_lock_resource = from_empty;
      if (!need_add_lock_resource) {
        for (auto& lock : transaction_ptr->lock_resource()) {
          if (lock == resource_uuid) {
            need_add_lock_resource = false;
            break;
          }
        }
      }
      if (need_add_lock_resource) {
        transaction_ptr->add_lock_resource(resource_uuid);
      }
    }

    auto old_holder = transaction_locks_.find(resource_uuid);
    if (old_holder != transaction_locks_.end() && old_holder->second) {
      unlock(old_holder->second, resource_uuid);
    }
    transaction_locks_[resource_uuid] = transaction_ptr;
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

bool transaction_participator_handle::unlock(const storage_ptr_type& transaction_ptr,
                                             const std::string& resource_uuid) noexcept {
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
                              [&resource_uuid](const std::string value) { return value == resource_uuid; });

  transaction_locks_.erase(lock_iter);
  return true;
}

bool transaction_participator_handle::unlock(const std::string& transaction_uuid,
                                             const std::string& resource_uuid) noexcept {
  auto transaction_iter = running_transactions_.find(transaction_uuid);
  if (transaction_iter == running_transactions_.end()) {
    return false;
  }

  if (!transaction_iter->second) {
    running_transactions_.erase(transaction_iter);
    return false;
  }

  return unlock(transaction_iter->second, resource_uuid);
}

bool transaction_participator_handle::unlock(const storage_ptr_type& transaction_ptr) noexcept {
  if (!transaction_ptr) {
    return false;
  }

  for (auto& resource_uuid : transaction_ptr->lock_resource()) {
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

bool transaction_participator_handle::unlock(const std::string& transaction_uuid) noexcept {
  auto transaction_iter = running_transactions_.find(transaction_uuid);
  if (transaction_iter == running_transactions_.end()) {
    return false;
  }

  if (!transaction_iter->second) {
    running_transactions_.erase(transaction_iter);
    return false;
  }
  return unlock(transaction_iter->second);
}

transaction_participator_handle::storage_ptr_type transaction_participator_handle::get_locker(
    const std::string& resource) const noexcept {
  auto iter = transaction_locks_.find(resource);
  if (iter == transaction_locks_.end()) {
    return nullptr;
  }

  if (iter->second->metadata().status() >=
      atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    return nullptr;
  }

  return iter->second;
}

const std::unordered_map<std::string, transaction_participator_handle::storage_ptr_type>&
transaction_participator_handle::get_running_transactions() const noexcept {
  return running_transactions_;
}

const std::unordered_map<std::string, transaction_participator_handle::storage_ptr_type>&
transaction_participator_handle::get_finished_transactions() const noexcept {
  return finished_transactions_;
}

rpc::result_code_type transaction_participator_handle::add_running_transcation(rpc::context& ctx,
                                                                               storage_type&& storage,
                                                                               storage_ptr_type& output) {
  if (storage.metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.add_running_transcation",
                         std::move(child_trace_option));

  bool trigger_event = false;
  auto& transaction_ptr = running_transactions_[storage.metadata().transaction_uuid()];
  if (!transaction_ptr) {
    transaction_ptr = std::make_shared<storage_type>(std::move(storage));
    trigger_event = true;
  } else {
    unlock(transaction_ptr);
  }
  if (!transaction_ptr) {
    RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC));
  }
  output = transaction_ptr;

  protobuf_move_message(*output, std::move(storage));

  // Reset timer
  protobuf_copy_message(*output->mutable_resolve_timepoint(), output->metadata().expire_timepoint());
  resolve_timers_.insert(storage_resolve_timer_type{*output});

  // Auto lock resource
  if (!output->lock_resource().empty()) {
    RPC_AWAIT_IGNORE_RESULT(lock(output, output->lock_resource()));
  }

  FWLOGDEBUG("participator {} start run transaction {}", get_participator_key(), output->metadata().transaction_uuid());
  // event callback
  if (trigger_event && vtable_ && vtable_->on_start_running) {
    rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(vtable_->on_start_running(child_ctx, *this, *output));
    if (res < 0) {
      FWLOGERROR("participator {} call on_start_running for transaction {} failed, error code: {}({})",
                 get_participator_key(), output->metadata().transaction_uuid(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

rpc::result_code_type transaction_participator_handle::remove_running_transaction(
    rpc::context& ctx, EnDistibutedTransactionStatus target_status, const std::string& transaction_uuid,
    storage_ptr_type* output) {
  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.remove_running_transaction",
                         std::move(child_trace_option));

  if (target_status < atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM));
  }

  auto iter = running_transactions_.find(transaction_uuid);
  if (iter == running_transactions_.end()) {
    FWLOGWARNING("participator {} try to remove transaction {} but not found", get_participator_key(),
                 transaction_uuid);
    RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
  }

  auto transaction_ptr = iter->second;
  if (nullptr != output) {
    *output = transaction_ptr;
  }

  if (transaction_ptr) {
    resolve_timers_.erase(storage_resolve_timer_type{*transaction_ptr});
  }
  running_transactions_.erase(iter);

  if (transaction_ptr) {
    FWLOGDEBUG("participator {} finish run transaction {}", get_participator_key(),
               transaction_ptr->metadata().transaction_uuid());
    // unlock resources
    unlock(transaction_ptr);

    // change status
    if (transaction_ptr->metadata().status() <
        atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
      transaction_ptr->mutable_metadata()->set_status(target_status);
    } else if (transaction_ptr->metadata().status() ==
                   atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING &&
               target_status == atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
      transaction_ptr->mutable_metadata()->set_status(target_status);
    } else if (transaction_ptr->metadata().status() ==
                   atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING &&
               target_status == atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
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

  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

rpc::result_code_type transaction_participator_handle::add_finished_transcation(
    rpc::context& ctx, const storage_ptr_type& transaction_ptr) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (transaction_ptr->metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.add_finished_transcation",
                         std::move(child_trace_option));

  finished_transactions_[transaction_ptr->metadata().transaction_uuid()] = transaction_ptr;

  FWLOGDEBUG("participator {} start finished transaction {}", get_participator_key(),
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

  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
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

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.remove_finished_transaction",
                         std::move(child_trace_option));

  FWLOGDEBUG("participator {} remove finished transaction {}", get_participator_key(),
             transaction_ptr->metadata().transaction_uuid());

  finished_transactions_.erase(iter);
  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

rpc::result_code_type transaction_participator_handle::resolve_transcation(rpc::context& ctx,
                                                                           const std::string& transaction_uuid) {
  storage_ptr_type transaction_ptr;
  {
    auto running_iter = running_transactions_.find(transaction_uuid);
    if (running_iter != running_transactions_.end()) {
      transaction_ptr = running_iter->second;
      if (!transaction_ptr) {
        running_transactions_.erase(running_iter);
      }
    }
  }

  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_NOT_FOUND);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.resolve_transcation",
                         std::move(child_trace_option));

  // Reset timer
  resolve_timers_.erase(storage_resolve_timer_type{*transaction_ptr});
  int64_t now_seconds = util::time::time_utility::get_now();
  int32_t now_nanos = static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000);
  if (now_nanos + transaction_ptr->configure().resolve_retry_interval().nanos() > 1000000000) {
    transaction_ptr->mutable_resolve_timepoint()->set_seconds(
        now_seconds + transaction_ptr->configure().resolve_retry_interval().seconds() + 1);
    transaction_ptr->mutable_resolve_timepoint()->set_nanos(
        now_nanos + transaction_ptr->configure().resolve_retry_interval().nanos() - 1000000000);
  } else {
    transaction_ptr->mutable_resolve_timepoint()->set_seconds(
        now_seconds + transaction_ptr->configure().resolve_retry_interval().seconds());
    transaction_ptr->mutable_resolve_timepoint()->set_nanos(
        now_nanos + transaction_ptr->configure().resolve_retry_interval().nanos());
  }
  resolve_timers_.insert(storage_resolve_timer_type{*transaction_ptr});

  // retry too many times and reject it directly
  transaction_ptr->set_resolve_times(transaction_ptr->resolve_times() + 1);
  if (transaction_ptr->resolve_times() > transaction_ptr->configure().resolve_max_times()) {
    FWLOGERROR("participator {} resolve transaction {} for more than {} times, just remove it", get_participator_key(),
               transaction_uuid, transaction_ptr->configure().resolve_max_times());
    return reject_transcation(child_ctx, transaction_uuid);
  }

  rpc::context::message_holder<atframework::distributed_system::transaction_blob_storage> trans_data(child_ctx);
  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(
      rpc::transaction_api::query_transaction(child_ctx, transaction_ptr->metadata(), *trans_data));
  if (res == PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND || res == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_NOT_FOUND) {
    FWLOGWARNING("participator {} resolve transaction {} but not found, just remove it", get_participator_key(),
                 transaction_uuid);
    RPC_RETURN_CODE(child_tracer.return_code(RPC_AWAIT_CODE_RESULT(remove_running_transaction(
        child_ctx, atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING, transaction_uuid,
        &transaction_ptr))));
  }

  if (res != 0) {
    FWLOGWARNING("participator {} resolve transaction {} failed, error code: {}({})", get_participator_key(),
                 transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(child_tracer.return_code(res));
  }

  rpc::transaction_api::merge_storage(get_participator_key(), *transaction_ptr, *trans_data);

  if (atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED ==
          transaction_ptr->metadata().status() ||
      atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING ==
          transaction_ptr->metadata().status()) {
    RPC_RETURN_CODE(child_tracer.return_code(RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, transaction_uuid))));
  } else if (atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED ==
                 transaction_ptr->metadata().status() ||
             atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING ==
                 transaction_ptr->metadata().status()) {
    RPC_RETURN_CODE(child_tracer.return_code(RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, transaction_uuid))));
  }

  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

rpc::result_code_type transaction_participator_handle::commit_transcation(rpc::context& ctx,
                                                                          const std::string& transaction_uuid) {
  // 如果移除了说明已经处理过了，不需要重复执行，这里支持并发
  auto iter = running_transactions_.find(transaction_uuid);
  if (iter == running_transactions_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto transaction_ptr = iter->second;
  if (!transaction_ptr) {
    running_transactions_.erase(iter);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.commit_transcation",
                         std::move(child_trace_option));

  FWLOGINFO("participator {} commit transaction {}", get_participator_key(), transaction_uuid);
  rpc::result_code_type::value_type res;
  // event callback
  if (vtable_ && vtable_->do_event) {
    res = RPC_AWAIT_CODE_RESULT(vtable_->do_event(child_ctx, *this, *transaction_ptr));
    if (res < 0) {
      FWLOGERROR("participator {} call do_event for transaction {} failed, error code: {}({})", get_participator_key(),
                 transaction_ptr->metadata().transaction_uuid(), res, protobuf_mini_dumper_get_error_msg(res));
    }
  }

  res = RPC_AWAIT_CODE_RESULT(remove_running_transaction(
      child_ctx, atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING, transaction_uuid,
      &transaction_ptr));
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

  // 每16个事务强制启动一次刷新操作
  if (5 == (finished_transactions_.size() & 15)) {
    tick(child_ctx, util::time::time_utility::now());
  }

  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

rpc::result_code_type transaction_participator_handle::reject_transcation(rpc::context& ctx,
                                                                          const std::string& transaction_uuid) {
  // 如果移除了说明已经处理过了，不需要重复执行，这里支持并发
  auto iter = running_transactions_.find(transaction_uuid);
  if (iter == running_transactions_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto transaction_ptr = iter->second;
  if (!transaction_ptr) {
    running_transactions_.erase(iter);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  rpc::context child_ctx{ctx};
  rpc::context::tracer child_tracer;
  rpc::context::trace_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.reject_transcation",
                         std::move(child_trace_option));

  rpc::result_code_type::value_type res;
  FWLOGINFO("participator {} reject transaction {}", get_participator_key(), transaction_uuid);
  res = RPC_AWAIT_CODE_RESULT(remove_running_transaction(
      child_ctx, atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING, transaction_uuid,
      &transaction_ptr));
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

  // 每16个事务强制启动一次刷新操作
  if (5 == (finished_transactions_.size() & 15)) {
    tick(child_ctx, util::time::time_utility::now());
  }
  RPC_RETURN_CODE(child_tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS));
}

}  // namespace distributed_system
}  // namespace atframework
