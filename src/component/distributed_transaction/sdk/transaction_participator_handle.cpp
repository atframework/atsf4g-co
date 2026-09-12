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

#include <atframe/atapp.h>

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <memory/object_allocator.h>

#include <dispatcher/task_manager.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/rpc_utils.h>

#include <logic/logic_server_setup.h>

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

static bool can_wound(const transaction_metadata& requester, const transaction_metadata& holder) {
  if (holder.status() >= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    return false;
  }
  if (requester.prepare_timepoint().seconds() != holder.prepare_timepoint().seconds()) {
    return requester.prepare_timepoint().seconds() < holder.prepare_timepoint().seconds();
  }
  if (requester.prepare_timepoint().nanos() != holder.prepare_timepoint().nanos()) {
    return requester.prepare_timepoint().nanos() < holder.prepare_timepoint().nanos();
  }
  return requester.transaction_uuid() < holder.transaction_uuid();
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
      const std::string& transaction_uuid, const transaction_participator_handle::storage_ptr_type& storage)
      : owner_(owner), transaction_uuid_(transaction_uuid), storage_(storage.get()) {}
  ~inflight_terminal_mark_guard() {
    auto iter = owner_.find(transaction_uuid_);
    if (iter != owner_.end() && iter->second.storage.get() == storage_) {
      iter->second.inflight_terminal_direction = transaction_participator_handle::terminal_direction_type::kNone;
    }
  }

  inflight_terminal_mark_guard(const inflight_terminal_mark_guard&) = delete;
  inflight_terminal_mark_guard& operator=(const inflight_terminal_mark_guard&) = delete;

 private:
  std::unordered_map<std::string, transaction_participator_handle::running_transaction_entry>& owner_;
  const std::string& transaction_uuid_;
  const transaction_participator_handle::storage_type* storage_;
};
}  // namespace

void transaction_participator_handle::resolve_timer_queue_type::insert_or_replace(resolve_timer_action_type action,
                                                                                  const storage_type& storage) {
  const std::string& transaction_uuid = storage.metadata().transaction_uuid();
  erase(transaction_uuid);
  storage_resolve_timer_type timer{action, storage};
  timers_.insert(timer);
  index_.insert_or_assign(transaction_uuid, timer);
  if (nullptr != on_change) {
    on_change(on_change_handle);
  }
}

void transaction_participator_handle::resolve_timer_queue_type::erase(const std::string& transaction_uuid) {
  auto iter = index_.find(transaction_uuid);
  if (iter == index_.end()) {
    return;
  }
  timers_.erase(iter->second);
  index_.erase(iter);
  if (nullptr != on_change) {
    on_change(on_change_handle);
  }
}

void transaction_participator_handle::resolve_timer_queue_type::clear() {
  bool had_entries = !timers_.empty();
  timers_.clear();
  index_.clear();
  if (had_entries && nullptr != on_change) {
    on_change(on_change_handle);
  }
}

void transaction_participator_handle::schedule_resolve_retry(resolve_timer_action_type action, storage_type& storage) {
  set_next_retry_timepoint(storage, atfw::util::time::time_utility::now());
  resolve_timer_queue_.insert_or_replace(action, storage);
}

bool transaction_participator_handle::is_current_transaction(resolve_timer_action_type action,
                                                             const storage_ptr_type& storage) const noexcept {
  if (!storage) {
    return false;
  }
  const auto& transaction_uuid = storage->metadata().transaction_uuid();
  if (action == resolve_timer_action_type::kQuery) {
    auto current = running_transactions_.find(transaction_uuid);
    return current != running_transactions_.end() && current->second.storage == storage;
  }
  auto current = finished_transactions_.find(transaction_uuid);
  return current != finished_transactions_.end() && current->second == storage;
}

void transaction_participator_handle::retry_resolve_transaction(resolve_timer_action_type action,
                                                                const storage_ptr_type& storage,
                                                                bool writable_check_failed) {
  if (!is_current_transaction(action, storage)) {
    return;
  }

  if (writable_check_failed) {
    const auto retry_limit = get_retry_limit(*storage);
    if (storage->resolve_times() < retry_limit) {
      storage->set_resolve_times(storage->resolve_times() + 1);
    }
    if (storage->resolve_times() >= retry_limit) {
      const auto& transaction_uuid = storage->metadata().transaction_uuid();
      FWLOGERROR("participator {} transaction {} exhausted {} attempts while checking writable, consume locally",
                 get_participator_key(), transaction_uuid, retry_limit);
      // 不可写时仅清理本地状态，不执行业务或完成回调，也不确认协调者决议。
      resolve_timer_queue_.erase(transaction_uuid);
      if (action == resolve_timer_action_type::kQuery) {
        if (storage->metadata().status() < atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
          storage->mutable_metadata()->set_status(
              atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING);
        }
        unlock(storage);
        running_transactions_.erase(transaction_uuid);
      } else {
        finished_transactions_.erase(transaction_uuid);
      }
      return;
    }
  }

  schedule_resolve_retry(action, *storage);
}

rpc::result_code_type transaction_participator_handle::finish_resolve_task(rpc::context& ctx) {
  if (task_type_trait::get_task_id(auto_resolve_transaction_task_) == ctx.get_task_context().task_id) {
    auto_resolve_transaction_task_.reset();
  }
  if (vtable_ && vtable_->on_resolve_task_finished) {
    RPC_AWAIT_IGNORE_RESULT(vtable_->on_resolve_task_finished(ctx, *this));
  }
  // 补驱动任务执行期间被跳过的 tick。
  tick(ctx, atfw::util::time::time_utility::now());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
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

  // 到期条目被移除后最早事件可能变化，通知维护方重排自定义定时器
  if (nullptr != on_change) {
    on_change(on_change_handle);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle::transaction_participator_handle(
    const atfw::util::memory::strong_rc_ptr<vtable_type>& vtable, gsl::string_view participator_key)
    : private_data_{nullptr}, on_destroy_{nullptr}, vtable_{vtable} {
  participator_key_.assign(participator_key.data(), participator_key.size());
  // 队列内容变化时把自定义定时器重新指向最早的到期事件（见 refresh_resolve_custom_timer）
  resolve_timer_queue_.on_change = &transaction_participator_handle::on_resolve_timer_queue_changed;
  resolve_timer_queue_.on_change_handle = this;
}

DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle::~transaction_participator_handle() {
  // 撤销仍在等待的自定义定时器，防止 handle 销毁后回调访问悬空 this；
  // 属主 app 已销毁时 watcher 已失效，remove_resolve_custom_timer 为空操作
  remove_resolve_custom_timer();
  if (nullptr != on_destroy_) {
    (*on_destroy_)(this);
  }
}

void transaction_participator_handle::on_resolve_timer_queue_changed(transaction_participator_handle* self) {
  if (nullptr != self) {
    self->refresh_resolve_custom_timer();
  }
}

void transaction_participator_handle::remove_resolve_custom_timer() noexcept {
  // 静态 remove_timer 直接按属主时间轮摘除（与 app::remove_custom_timer 内部路径一致），
  // 不需要经过 atapp::app::get_last_instance()
  auto timer = resolve_custom_timer_watcher_.lock();
  if (timer) {
    atapp_timer_t::remove_timer(*timer);
  }
  resolve_custom_timer_watcher_.reset();
  resolve_custom_timer_timepoint_ = {};
}

void transaction_participator_handle::refresh_resolve_custom_timer() {
  const auto* earliest = resolve_timer_queue_.earliest();
  if (nullptr == earliest) {
    // 队列已空：撤销定时器，不再自驱动
    remove_resolve_custom_timer();
    return;
  }

  // 最早事件未变化且定时器仍在等待：直接复用，避免 load 等批量操作反复注销重建
  if (!resolve_custom_timer_watcher_.expired() && resolve_custom_timer_timepoint_ == earliest->timepoint) {
    return;
  }

  // 无 atapp 实例的嵌入场景：不注册定时器，维持原有的外部 tick() 驱动契约
  auto* owner_app = atframework::atapp::app::get_last_instance();
  if (nullptr == owner_app) {
    return;
  }

  remove_resolve_custom_timer();
  int res = owner_app->add_custom_timer_with_system_clock(
      earliest->timepoint,
      [this](time_t /*tick_time*/, const atapp_timer_t::timer_t&) { on_resolve_custom_timer_fired(); }, this,
      &resolve_custom_timer_watcher_);
  if (0 != res) {
    // 注册失败（如超时超出时间轮上限）：保持无定时器状态，由外部 tick()/finished 计数驱动兜底
    FWLOGERROR("participator {} register resolve custom timer failed, res: {}", get_participator_key(), res);
    resolve_custom_timer_timepoint_ = {};
    return;
  }
  resolve_custom_timer_timepoint_ = earliest->timepoint;
}

void transaction_participator_handle::on_resolve_custom_timer_fired() {
  // 定时器已触发并从时间轮移除，watcher 随之失效；清空记录的时间点以允许按原时刻重新注册
  resolve_custom_timer_watcher_.reset();
  resolve_custom_timer_timepoint_ = {};

  tick(logic_server_get_current_tick_context(), atframework::atapp::app::get_sys_now());
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
DISTRIBUTED_TRANSACTION_SDK_API bool transaction_participator_handle::has_resolve_custom_timer_for_unit_test()
    const noexcept {
  return !resolve_custom_timer_watcher_.expired();
}

DISTRIBUTED_TRANSACTION_SDK_API atfw::util::time::time_utility::raw_time_t
transaction_participator_handle::get_resolve_custom_timer_timepoint_for_unit_test() const noexcept {
  return resolve_custom_timer_timepoint_;
}

DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle::resolve_custom_timer_watcher_for_unit_test_type
transaction_participator_handle::get_resolve_custom_timer_watcher_for_unit_test() const noexcept {
  return resolve_custom_timer_watcher_;
}

DISTRIBUTED_TRANSACTION_SDK_API void transaction_participator_handle::fire_resolve_custom_timer_for_unit_test(
    atfw::util::time::time_utility::raw_time_t timepoint) {
  remove_resolve_custom_timer();
  tick(logic_server_get_current_tick_context(), timepoint);
}
#endif

DISTRIBUTED_TRANSACTION_SDK_API void transaction_participator_handle::load(const snapshot_type& storage) {
  resolve_timer_queue_.clear();
  running_transactions_.clear();
  transaction_locks_.clear();
  finished_transactions_.clear();
  // 尚未结束的任务通过对象身份校验丢弃旧结果；业务数据与加载的快照须由接入方保持一致。

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
    running_entry.local_action_stage_entered =
        transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING ||
        transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED;

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

  for (const auto& transaction_uuid : storage.wounded_transaction_uuid()) {
    auto iter = running_transactions_.find(transaction_uuid);
    if (iter != running_transactions_.end() &&
        iter->second.storage->metadata().status() <
            atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
      iter->second.wounded = true;
    }
  }

  for (const auto& transaction : storage.finished_transaction()) {
    const std::string& transaction_uuid = transaction.metadata().transaction_uuid();
    if (transaction_uuid.empty()) {
      FWLOGERROR("participator {} load snapshot with empty finished transaction uuid, skip it", get_participator_key());
      continue;
    }
    if (running_transactions_.end() != running_transactions_.find(transaction_uuid) ||
        finished_transactions_.end() != finished_transactions_.find(transaction_uuid)) {
      FWLOGERROR("participator {} load snapshot with finished transaction {} already present, skip it",
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
      if (transaction.second.wounded) {
        storage.add_wounded_transaction_uuid(transaction.first);
      }
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
    // task_action_participator_resolve_transaction） 会 兜底 tick()/refresh 恢复定时器
    return 0;
  }

  std::list<storage_ptr_type> pending_transactions;
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

      pending_transactions.push_back(transaction_iter->second.storage);
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
  auto rearm_timers = [this](const std::list<storage_ptr_type>& pending,
                             const std::vector<storage_ptr_type>& finished) {
    for (const auto& transaction_ptr : pending) {
      retry_resolve_transaction(resolve_timer_action_type::kQuery, transaction_ptr, false);
    }
    for (const auto& transaction_ptr : finished) {
      retry_resolve_transaction(resolve_timer_action_type::kAcknowledge, transaction_ptr, false);
    }
  };

  task_action_participator_resolve_transaction::ctor_param_t params;
  params.participantor = shared_from_this();
  // 原列表保留给失败分支，任务参数使用副本。
  params.pending_transactions = pending_transactions;
  params.submmit_transactions = submmit_transactions;

  int32_t res = task_manager::me()->create_task<task_action_participator_resolve_transaction>(
      auto_resolve_transaction_task_, std::move(params));
  if (0 != res || task_type_trait::empty(auto_resolve_transaction_task_)) {
    FWLOGERROR("create task_action_participator_resolve_transaction failed, res: {}({})", res,
               protobuf_mini_dumper_get_error_msg(res));
    rearm_timers(pending_transactions, submmit_transactions);
    return res;
  }

  dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
  res = task_manager::me()->start_task(task_type_trait::get_task_id(auto_resolve_transaction_task_), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_participator_resolve_transaction {} failed, res: {}({})",
               task_type_trait::get_task_id(auto_resolve_transaction_task_), res,
               protobuf_mini_dumper_get_error_msg(res));
    task_type_trait::reset_task(auto_resolve_transaction_task_);
    rearm_timers(pending_transactions, submmit_transactions);
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
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/prepare"},
  };

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
      RPC_RETURN_CODE(child_tracer.finish({res, {}}));
    }
  }

  if (request.storage().metadata().status() < atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    request.mutable_storage()->mutable_metadata()->set_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
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

    request.mutable_storage()->mutable_metadata()->set_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);
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

    request.mutable_storage()->mutable_metadata()->set_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
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
    auto res = RPC_AWAIT_CODE_RESULT(add_running_transcation(child_ctx, std::move(*request.mutable_storage()), output));
    if (res == PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) {
      response.mutable_reason()->set_allow_retry(true);
    }
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type transaction_participator_handle::commit(
    rpc::context& ctx, const SSParticipatorTransactionCommitReq& request, SSParticipatorTransactionCommitRsp&) {
  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/commit"},
  };

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
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/reject"},
  };

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.reject", std::move(child_trace_option));

  if (request.has_storage() && request.storage().configure().force_commit()) {
    // 公开 RPC 可能收到畸形/伪造 payload，undo 前必须验证内外 UUID 一致；内部 UUID 缺失同样视为非法
    if (request.storage().metadata().transaction_uuid().empty() ||
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
      if (old_holder->second->metadata().transaction_uuid() == metadata.transaction_uuid()) {
        break;
      }

      if (!can_wound(metadata, old_holder->second->metadata())) {
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

  // check_prepare 可以切出；任一资源不能 wound 时直接返回，不改锁或其他事务。
  bool has_conflict = false;
  for (const auto& resource_uuid : resource_uuids) {
    auto holder = transaction_locks_.find(resource_uuid);
    if (holder == transaction_locks_.end() || !holder->second || holder->second == transaction_ptr) {
      continue;
    }
    auto wounded_iter = running_transactions_.find(holder->second->metadata().transaction_uuid());
    if (!can_wound(transaction_ptr->metadata(), holder->second->metadata()) ||
        wounded_iter == running_transactions_.end() || wounded_iter->second.storage != holder->second ||
        wounded_iter->second.inflight_terminal_direction != terminal_direction_type::kNone) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED);
    }
    has_conflict = true;
  }
  if (has_conflict) {
    for (const auto& resource_uuid : resource_uuids) {
      auto holder = transaction_locks_.find(resource_uuid);
      if (holder == transaction_locks_.end() || !holder->second || holder->second == transaction_ptr) {
        continue;
      }
      auto wounded_iter = running_transactions_.find(holder->second->metadata().transaction_uuid());
      if (!wounded_iter->second.wounded) {
        wounded_iter->second.wounded = true;
        // 沿用原超时恢复时间，wound 不提前启动未知决议的重试。
        resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kQuery, *holder->second);
      }
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED);
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
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/add_running_transcation"},
  };

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
    running_transactions_.erase(transaction_uuid);
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC, {}}));
  }
  output = transaction_ptr;

  protobuf_move_message(*output, std::move(storage));

  // Reset timer
  protobuf_copy_message(*output->mutable_resolve_timepoint(), output->metadata().expire_timepoint());
  resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kQuery, *output);

  // Auto lock resource
  if (!output->lock_resource().empty()) {
    const auto lock_result = RPC_AWAIT_CODE_RESULT(lock(output, output->lock_resource()));
    if (lock_result < 0) {
      resolve_timer_queue_.erase(transaction_uuid);
      running_transactions_.erase(transaction_uuid);
      output.reset();
      RPC_RETURN_CODE(child_tracer.finish({lock_result, {}}));
    }
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
      {
          opentelemetry::semconv::rpc::kRpcMethod,
          "atframework.transaction_participator_handle/remove_running_transaction",
      },
  };

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
    FWLOGWARNING("participator {} try to remove transaction {} but not found, maybe already finished before",
                 get_participator_key(), transaction_uuid);
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }

  auto transaction_ptr = iter->second.storage;
  if (nullptr != output) {
    *output = transaction_ptr;
  }
  const bool is_terminal_transition = iter->second.inflight_terminal_direction != terminal_direction_type::kNone;

  resolve_timer_queue_.erase(transaction_uuid);
  // 条目销毁即完成全部运行时标记（wound/action-stage/inflight）的生命周期清理
  running_transactions_.erase(iter);

  if (transaction_ptr) {
    FWLOGDEBUG("participator {} finish run transaction {}", get_participator_key(),
               transaction_ptr->metadata().transaction_uuid());
    // unlock resources
    unlock(transaction_ptr);

    // 仅推进同一方向的状态；迁入 finished 不回退已知的最终决议。
    const auto current_status = transaction_ptr->metadata().status();
    if (current_status < atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED ||
        (current_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING &&
         target_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) ||
        (current_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING &&
         target_status == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED)) {
      transaction_ptr->mutable_metadata()->set_status(target_status);
    }

    // 在生命周期回调切出前登记 finished，避免同 UUID 在两个集合之间的空窗被重新 prepare。
    // acknowledge 定时器及 on_finished 由 start_finished_transaction 按原顺序触发。
    if (is_terminal_transition) {
      transaction_ptr->set_resolve_times(0);
      protobuf_copy_message(*transaction_ptr->mutable_resolve_timepoint(),
                            protobuf_from_system_clock(atfw::util::time::time_utility::now()));
      finished_transactions_[transaction_uuid] = transaction_ptr;
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

rpc::result_code_type transaction_participator_handle::start_finished_transaction(
    rpc::context& ctx, const storage_ptr_type& transaction_ptr) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (transaction_ptr->metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {
          opentelemetry::semconv::rpc::kRpcMethod,
          "atframework.transaction_participator_handle/start_finished_transaction",
      },
  };

  rpc::context child_ctx{ctx};
  rpc::telemetry::tracer child_tracer;
  rpc::telemetry::trace_start_option child_trace_option;
  child_trace_option.dispatcher = nullptr;
  child_trace_option.is_remote = false;
  child_trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  child_trace_option.attributes = trace_attributes;

  child_ctx.setup_tracer(child_tracer, "transaction_participator_handle.start_finished_transaction",
                         std::move(child_trace_option));

  if (!is_current_transaction(resolve_timer_action_type::kAcknowledge, transaction_ptr)) {
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }
  if (transaction_ptr->finished_callback_completed()) {
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }
  resolve_timer_queue_.erase(transaction_ptr->metadata().transaction_uuid());

  FWLOGDEBUG("participator {} add finished transaction {}", get_participator_key(),
             transaction_ptr->metadata().transaction_uuid());

  // event callback
  rpc::result_code_type::value_type res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (vtable_ && vtable_->on_finished) {
    res = RPC_AWAIT_CODE_RESULT(vtable_->on_finished(child_ctx, *this, *transaction_ptr));
  }
  if (!is_current_transaction(resolve_timer_action_type::kAcknowledge, transaction_ptr)) {
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
  }
  if (res < 0) {
    FWLOGERROR("participator {} call on_finished for transaction {} failed, error code: {}({})", get_participator_key(),
               transaction_ptr->metadata().transaction_uuid(), res, protobuf_mini_dumper_get_error_msg(res));
    const auto retry_limit = get_retry_limit(*transaction_ptr);
    if (transaction_ptr->resolve_times() < retry_limit) {
      transaction_ptr->set_resolve_times(transaction_ptr->resolve_times() + 1);
    }
    if (transaction_ptr->resolve_times() >= retry_limit) {
      FWLOGERROR("participator {} exhausted {} on_finished attempts for transaction {}, consume locally",
                 get_participator_key(), retry_limit, transaction_ptr->metadata().transaction_uuid());
      RPC_AWAIT_IGNORE_RESULT(remove_finished_transaction(child_ctx, transaction_ptr));
    } else {
      schedule_resolve_retry(resolve_timer_action_type::kAcknowledge, *transaction_ptr);
    }
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
  }

  transaction_ptr->set_finished_callback_completed(true);
  transaction_ptr->set_resolve_times(0);
  if (vtable_) {
    const bool committed =
        transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING ||
        transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED;
    auto& callback = committed ? vtable_->on_commited : vtable_->on_rejected;
    if (callback) {
      res = RPC_AWAIT_CODE_RESULT(callback(child_ctx, *this, *transaction_ptr));
      if (res < 0) {
        FWLOGERROR("participator {} call {} for transaction {} failed, error code: {}({})", get_participator_key(),
                   committed ? "on_commited" : "on_rejected", transaction_ptr->metadata().transaction_uuid(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }
  }

  // 完成通知可能切出；返回前不允许 ACK 改变回调正在观察的状态或清理该事务。
  // 回调中 load 后，只能为仍属于当前 finished 集合的对象登记定时器。
  if (is_current_transaction(resolve_timer_action_type::kAcknowledge, transaction_ptr)) {
    protobuf_copy_message(*transaction_ptr->mutable_resolve_timepoint(),
                          protobuf_from_system_clock(atfw::util::time::time_utility::now()));
    resolve_timer_queue_.insert_or_replace(resolve_timer_action_type::kAcknowledge, *transaction_ptr);
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
      {
          opentelemetry::semconv::rpc::kRpcMethod,
          "atframework.transaction_participator_handle/remove_finished_transaction",
      },
  };

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
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/resolve_transcation"},
  };

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
    if (transaction_ptr->resolve_times() >= get_retry_limit(*transaction_ptr)) {
      RPC_RETURN_CODE(
          child_tracer.finish({RPC_AWAIT_CODE_RESULT(remove_running_transaction(
                                   child_ctx, atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                                   transaction_uuid, &transaction_ptr)),
                               {}}));
    }
    transaction_ptr->set_resolve_times(transaction_ptr->resolve_times() + 1);
    schedule_resolve_retry(resolve_timer_action_type::kQuery, *transaction_ptr);
    auto decision = rpc::make_shared_message<transaction_metadata>(child_ctx);
    protobuf_copy_message(*decision, transaction_ptr->metadata());
    auto res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_transaction(child_ctx, *decision));
    auto current = running_transactions_.find(transaction_uuid);
    if (current == running_transactions_.end() || current->second.storage != transaction_ptr ||
        !current->second.wounded || current->second.inflight_terminal_direction != terminal_direction_type::kNone) {
      RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
    }
    // NOTFOUND 或有效成功响应不足 R 时，使用剩余次数重试，不重置计数。
    if (res < 0) {
      RPC_RETURN_CODE(child_tracer.finish({res, {}}));
    }
    if (decision->transaction_uuid() != transaction_uuid ||
        (decision->status() != atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED &&
         decision->status() != atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED)) {
      RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_RETRY_TIMES_EXCEED, {}}));
    }
    current->second.wounded = false;
    rpc::transaction_api::merge_metadata(*transaction_ptr->mutable_metadata(), *decision);
    if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
      RPC_RETURN_CODE(
          child_tracer.finish({RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, transaction_uuid)), {}}));
    }
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(reject_transcation(child_ctx, transaction_uuid)), {}}));
  }

  if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED ||
      transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING) {
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, transaction_uuid)), {}}));
  }
  if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING ||
      transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
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
  const auto queried_status = transaction_ptr->metadata().status();
  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(
      rpc::transaction_api::query_transaction(child_ctx, transaction_ptr->metadata(), *trans_data));
  auto current = running_transactions_.find(transaction_uuid);
  if (current == running_transactions_.end() || current->second.storage != transaction_ptr ||
      transaction_ptr->metadata().status() != queried_status || current->second.wounded ||
      current->second.inflight_terminal_direction != terminal_direction_type::kNone) {
    // 等待期间可能 load、wound、commit 或 reject；旧响应不能删除或改写当前事务。
    RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
  }
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
  if (atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING == transaction_ptr->metadata().status() ||
      atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED == transaction_ptr->metadata().status()) {
    RPC_RETURN_CODE(child_tracer.finish({RPC_AWAIT_CODE_RESULT(commit_transcation(child_ctx, transaction_uuid)), {}}));
  } else if (atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING ==
                 transaction_ptr->metadata().status() ||
             atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED ==
                 transaction_ptr->metadata().status()) {
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
  if (iter->second.inflight_terminal_direction != terminal_direction_type::kNone) {
    if (iter->second.inflight_terminal_direction == terminal_direction_type::kCommit) {
      // 同方向重复调用：正在执行的流程会完成相同动作，幂等返回
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    FWLOGERROR("participator {} commit transaction {} but a reject transition is in flight", get_participator_key(),
               transaction_uuid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING ||
      transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/commit_transcation"},
  };

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
  iter->second.inflight_terminal_direction = terminal_direction_type::kCommit;
  iter->second.wounded = false;
  inflight_terminal_mark_guard inflight_guard{running_transactions_, transaction_uuid, transaction_ptr};

  FWLOGINFO("participator {} commit transaction {}", get_participator_key(), transaction_uuid);
  rpc::result_code_type::value_type res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  // 动作阶段计数只在首次进入时重置，与是否设置 do_event 无关。
  if (!iter->second.local_action_stage_entered) {
    iter->second.local_action_stage_entered = true;
    transaction_ptr->set_resolve_times(0);
  }
  if (transaction_ptr->metadata().status() < atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
    transaction_ptr->mutable_metadata()->set_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);
  }
  if (vtable_ && vtable_->do_event) {
    res = RPC_AWAIT_CODE_RESULT(vtable_->do_event(child_ctx, *this, *transaction_ptr));
    auto current = running_transactions_.find(transaction_uuid);
    if (current == running_transactions_.end() || current->second.storage != transaction_ptr) {
      RPC_RETURN_CODE(child_tracer.finish({res, {}}));
    }
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
  }

  res = RPC_AWAIT_CODE_RESULT(
      remove_running_transaction(child_ctx, atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                                 transaction_uuid, &transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call remove running transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
  }
  res = RPC_AWAIT_CODE_RESULT(start_finished_transaction(child_ctx, transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call start finished transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
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
  if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED ||
      transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  // 同一事务的最终状态流程互斥：同一事务同一时刻只允许一个方向的流程在执行
  if (iter->second.inflight_terminal_direction != terminal_direction_type::kNone) {
    if (iter->second.inflight_terminal_direction == terminal_direction_type::kReject) {
      // 同方向重复调用：正在执行的流程会完成相同动作，幂等返回
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    FWLOGERROR("participator {} reject transaction {} but a commit transition is in flight", get_participator_key(),
               transaction_uuid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }

  rpc::telemetry::trace_attribute_pair_type trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "atframework.transaction_participator_handle/reject_transcation"},
  };

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
  iter->second.inflight_terminal_direction = terminal_direction_type::kReject;
  iter->second.wounded = false;
  inflight_terminal_mark_guard inflight_guard{running_transactions_, transaction_uuid, transaction_ptr};

  rpc::result_code_type::value_type res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  FWLOGINFO("participator {} reject transaction {}", get_participator_key(), transaction_uuid);

  res = RPC_AWAIT_CODE_RESULT(
      remove_running_transaction(child_ctx, atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                                 transaction_uuid, &transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call remove running transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
  }
  res = RPC_AWAIT_CODE_RESULT(start_finished_transaction(child_ctx, transaction_ptr));
  if (res < 0) {
    FWLOGERROR("participator {} call start finished transaction for transaction {} failed, error code: {}({})",
               get_participator_key(), transaction_uuid, res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(child_tracer.finish({res, {}}));
  }

  // 执行中标记由 guard 析构复位；remove_running_transaction 销毁条目时析构为空操作

  // 每16个事务强制启动一次刷新操作
  if (kFinishedTransactionAutoTickPhase == (finished_transactions_.size() % kFinishedTransactionAutoTickModulo)) {
    tick(child_ctx, atfw::util::time::time_utility::now());
  }
  RPC_RETURN_CODE(child_tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SUCCESS, {}}));
}

rpc::result_code_type transaction_participator_handle::handle_finished_transaction_result(
    rpc::context& ctx, const storage_ptr_type& transaction_ptr, int32_t result, const metadata_type& metadata) {
  if (!transaction_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (!is_current_transaction(resolve_timer_action_type::kAcknowledge, transaction_ptr)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (result >= 0) {
    // ACK 成功才完成本地确认；查询或快照中已经获知的终态保持不变。
    if (transaction_ptr->metadata().status() == atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING) {
      transaction_ptr->mutable_metadata()->set_status(
          atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
    } else if (transaction_ptr->metadata().status() ==
               atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING) {
      transaction_ptr->mutable_metadata()->set_status(
          atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
    }
    rpc::transaction_api::merge_metadata(*transaction_ptr->mutable_metadata(), metadata);
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
