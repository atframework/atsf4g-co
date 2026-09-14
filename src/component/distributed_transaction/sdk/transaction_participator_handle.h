// Copyright 2022 atframework
// Created by owent, on 2022-02-28

#pragma once

#include <config/compile_optimize.h>
#include <config/compiler_features.h>

// clanfg-format off
#include <config/compiler/protobuf_prefix.h>
// clanfg-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>

// clanfg-format off
#include <config/compiler/protobuf_suffix.h>
// clanfg-format on

#include <gsl/select-gsl.h>
#include <nostd/function_ref.h>
#include <time/jiffies_timer.h>
#include <time/time_utility.h>

#include <config/server_frame_build_feature.h>
#include <dispatcher/task_type_traits.h>

#include <rpc/rpc_common_types.h>
#include <utility/protobuf_mini_dumper.h>

#include <memory/rc_ptr.h>
#include <functional>
#include <list>
#include <set>
#include <string>
#include <unordered_map>

namespace rpc {
class context;
}

namespace atframework {
namespace distributed_system {

class task_action_participator_resolve_transaction;

class transaction_participator_handle
    : public atfw::util::memory::enable_shared_rc_from_this<transaction_participator_handle> {
 public:
  using storage_type = atfw::distributed_system::transaction_participator_storage;
  using metadata_type = atfw::distributed_system::transaction_metadata;
  using configure_type = atfw::distributed_system::transaction_configure;
  using snapshot_type = atfw::distributed_system::transaction_participator_snapshot;
  using storage_ptr_type = atfw::util::memory::strong_rc_ptr<storage_type>;
  using storage_const_ptr_type = atfw::util::memory::strong_rc_ptr<const storage_type>;

  struct ATFW_UTIL_SYMBOL_VISIBLE vtable_type {
    // 事务执行(Do)回调。失败重试或快照恢复可能重放，必须幂等，并与 SDK 快照一致保存业务结果。
    // 不可写重试耗尽会强制解锁，在途回调的业务写入须校验资源所有权或版本。
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)> do_event;

    // 事务回滚(Undo)回调,仅仅在 force_commit=true 时才会触发。
    // force_commit 是 best-effort 模型：没有协调者记录、参与者持久状态和定时恢复，
    // 因此 undo_event 必须支持在未执行过 do_event 时按 no-op 成功返回，并且必须可重放（幂等）。
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        undo_event;

    // 执行预检查的回调。callback 内不允许修改 storage 的 transaction_uuid/时间字段。
    // 如果允许发起者重试请设置 transaction_participator_failure_reason.allow_retry = true
    // 如果是锁被抢占请 transaction_participator_failure_reason.locked_resource 设置为被抢占的资源,
    //   并且设置 transaction_participator_failure_reason.allow_retry = true
    //   同时返回码设为 0 或者 EN_TRANSACTION_RESOURCE_PREEMPTED
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, storage_type&,
                                        transaction_participator_failure_reason&)>
        check_prepare;
    // 同步检查，不切出协程。恢复任务中报错或不可写计入当前阶段重试次数；耗尽后仅清理 SDK 本地状态。
    std::function<int32_t(rpc::context&, transaction_participator_handle&, bool&)> check_writable;

    // 两种模型的新事务准备成功后均从 PREPARED 开始；普通事务重复 prepare 不重放本回调。
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_start_running;
    // 事务离开 running 集合时触发（含 resolve 时协调者记录 NOTFOUND 的本地清理路径）。
    // 注意 NOTFOUND 清理只触发本回调，不会触发 on_finished/on_rejected（事务没有全局最终状态）。
    // 不可写重试耗尽的强制清理不触发本回调。
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_finish_running;
    // 本地动作及 on_finished 完成后的通知；回调返回后才启动协调者 ACK，不表示已收到协调者确认。
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_commited;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_rejected;
    // 普通事务失败按配置有限重试；成功后启动 ACK，耗尽则清理本地记录。回调不改变事务状态。
    // 快照恢复可能重放本回调，须幂等。force_commit 失败仍按 best-effort 记录日志后继续。
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_finished;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&)> on_resolve_task_finished;
  };

  using on_destroy_callback_type = void (*)(transaction_participator_handle*);

 public:
  transaction_participator_handle(const transaction_participator_handle&) = delete;
  transaction_participator_handle(transaction_participator_handle&&) = delete;
  transaction_participator_handle& operator=(const transaction_participator_handle&) = delete;
  transaction_participator_handle& operator=(transaction_participator_handle&&) = delete;

  DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle(
      const atfw::util::memory::strong_rc_ptr<vtable_type>& vtable, gsl::string_view participator_key);
  DISTRIBUTED_TRANSACTION_SDK_API ~transaction_participator_handle();

  ATFW_UTIL_FORCEINLINE void* get_private_data() const noexcept { return private_data_; }
  ATFW_UTIL_FORCEINLINE void set_private_data(void* ptr) noexcept { private_data_ = ptr; }
  ATFW_UTIL_FORCEINLINE on_destroy_callback_type get_on_destroy_callback() const noexcept { return on_destroy_; }
  ATFW_UTIL_FORCEINLINE void set_on_destroy_callback(on_destroy_callback_type fn) noexcept { on_destroy_ = fn; }

  ATFW_UTIL_FORCEINLINE const std::string& get_participator_key() const noexcept { return participator_key_; }

  DISTRIBUTED_TRANSACTION_SDK_API void load(const snapshot_type& storage);

  DISTRIBUTED_TRANSACTION_SDK_API void dump(snapshot_type& storage);

  /**
   * @brief Tick, this function should be called after prepare/commit/reject and interval
   *
   * @param ctx rpc context used to create new task
   * @param timepoint current timepoint
   * @return 0, or error code of creating/starting the auto resolve task
   */
  DISTRIBUTED_TRANSACTION_SDK_API int32_t tick(rpc::context& ctx, atfw::util::time::time_utility::raw_time_t timepoint);

  /**
   * @brief Check writable of current transaction participator
   *
   * @param ctx current callback context
   * @param writable output if it's writable now
   * @return 0 or error code; this check does not suspend
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API int32_t check_writable(rpc::context& ctx,
                                                                                      bool& writable);

  /**
   * @brief Prepare for transaction
   * @note If force_commit = true, then vtable.do_event(...) will be called immediately
   *   after check_prepare succeeds without allow_retry and all requested resources are locked.
   *   force_commit 是 best-effort 模型，不是可容灾 2PC：
   *   - 参与者不进入 running/finished 集合，不创建 resolve timer，也没有协调者记录；
   *   - lock_resource 在启动回调前加锁，do_event 返回后、on_finish_running 前解锁；退出时也会清理；
   *   - 生命周期回调顺序为 on_start_running -> do_event -> on_finish_running，
   *     仅当 do_event 成功时才追加 on_finished -> on_commited，do_event 失败时不触发 on_finished/on_rejected；
   *   - 补偿（undo）只存在于 client 本次调用的有限次重试内，client/参与者故障可能永久部分执行，
   *     因此 force_commit 的 do_event/undo_event 都必须幂等，undo_event 必须支持 no-op 和重放。
   *   普通模式下重复 prepare 是幂等的：running 中的事务返回现有对象，已 finished 的事务返回 finished 副本，
   *   不会覆盖正在执行最终状态动作的事务，也不会重新触发生命周期事件。
   *
   * @param ctx rpc context
   * @param request RPC request
   * @param response RPC response
   * @param output output the created transaction
   *
   * @return future of 0 or error code
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type prepare(
      rpc::context& ctx, SSParticipatorTransactionPrepareReq&& request, SSParticipatorTransactionPrepareRsp& response,
      storage_ptr_type& output);

  /**
   * @brief Notify to commit transaction
   *
   * @param ctx rpc context
   * @param transaction_uuid transaction uuid
   *
   * @return future of 0 or error code
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type commit(
      rpc::context& ctx, const SSParticipatorTransactionCommitReq& request,
      SSParticipatorTransactionCommitRsp& response);

  /**
   * @brief Notify to reject for transaction
   *
   * @param ctx rpc context
   * @param transaction_uuid transaction uuid
   *
   * @return future of 0 or error code
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type reject(
      rpc::context& ctx, const SSParticipatorTransactionRejectReq& request,
      SSParticipatorTransactionRejectRsp& response);

  /**
   * @brief Check if we can lock resource into specify transaction
   *
   * @param metadata transaction metadata
   * @param resource_uuids resource uuid to check
   * @param preemption_transaction output current preemption transaction of this resource when return
   * EN_TRANSACTION_RESOURCE_PREEMPTED
   *
   * @note Wound-Wait: check_lock 只检查年龄和状态；check_prepare 可 await，lock 会再次检查。
   *   更老的事务可登记 wound，但全局决议确认前不会转移原锁，竞争者须重试。
   * @see http://www.mathcs.emory.edu/~cheung/Courses/554/Syllabus/8-recv+serial/deadlock-compare.html
   *
   * @return 0 or error code
   */
  DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type::value_type check_lock(
      const transaction_metadata& metadata, gsl::span<const std::string> resource_uuids,
      std::list<storage_const_ptr_type>& preemption_transaction);

  /**
   * @brief Lock resource into specify transaction
   * @note This function should be called after check_lock
   *
   * @param transaction_ptr transaction
   * @param resource_uuids resource uuids to lock
   *
   * @note 冲突时保留原有锁并返回 EN_TRANSACTION_RESOURCE_PREEMPTED，不登记任何新资源。
   *   普通事务必须使用当前 running 集合内的 storage；未登记或 load 后的旧对象不能加锁或 wound。
   *   force_commit 允许本次调用持锁；直接调用 lock 的接入方须在调用结束前用 storage 句柄 unlock。
   *   任一资源不可抢占时不 wound 其他事务；全部检查通过后才登记 wound。
   *   wound 保留原恢复时间和重试计数；收到决议或超时恢复后，在剩余重试次数内完成处理，达到上限则清理。
   * @see http://www.mathcs.emory.edu/~cheung/Courses/554/Syllabus/8-recv+serial/deadlock-compare.html
   *
   * @return future of 0 or error code
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type lock(
      const storage_ptr_type& transaction_ptr, const google::protobuf::RepeatedPtrField<std::string>& resource_uuids);

  /**
   * @brief Unlock resource from specify transaction
   *
   * @param transaction_ptr transaction
   * @param resource_uuid resource uuid
   *
   * @return true for success and false for failure or not found
   */
  DISTRIBUTED_TRANSACTION_SDK_API bool unlock(const storage_ptr_type& transaction_ptr,
                                              const std::string& resource_uuid) noexcept;

  /**
   * @brief Unlock resource from specify transaction
   *
   * @param transaction_uuid transaction uuid
   * @param resource_uuid resource uuid
   *
   * @return true for success and false for failure or not found
   */
  DISTRIBUTED_TRANSACTION_SDK_API bool unlock(const std::string& transaction_uuid,
                                              const std::string& resource_uuid) noexcept;

  /**
   * @brief Unlock all resource from specify transaction
   *
   * @param transaction_ptr transaction
   *
   * @return true for success and false for failure or not found
   */
  DISTRIBUTED_TRANSACTION_SDK_API bool unlock(const storage_ptr_type& transaction_ptr) noexcept;

  /**
   * @brief Unlock all resource from specify transaction
   *
   * @param transaction_uuid transaction uuid
   *
   * @return true for success and false for failure or not found
   */
  DISTRIBUTED_TRANSACTION_SDK_API bool unlock(const std::string& transaction_uuid) noexcept;

  /**
   * @brief Get the locker transaction by resource key
   *
   * @param resource resource key
   *
   * @return The transaction by which this resource is locked, or nullptr if this resource is not locked
   */
  DISTRIBUTED_TRANSACTION_SDK_API storage_ptr_type get_locker(const std::string& resource) const noexcept;

  enum class terminal_direction_type : int32_t { kNone, kCommit, kReject };

  // running 事务条目：事务 storage 与其全部运行时标记同条目同生命周期，随 running 集合创建/销毁，
  // 避免多个平行容器（wound/action-stage/inflight 标记）与 running 集合之间的隐式生命周期关联
  struct running_transaction_entry {
    storage_ptr_type storage;
    // on_start_running 返回前不启动恢复，commit/reject 返回 EN_SYS_BUSY，保证生命周期事件顺序。
    // 仅为当前回调保留的运行时标记；load 恢复的事务不重放 on_start_running。
    bool start_callback_running = false;
    // 正在执行的最终状态流程（do_event/迁移到 finished）方向，用于同一事务内的流程互斥；
    // kNone 表示当前没有正在执行的流程，与 metadata 中的事务状态独立。
    terminal_direction_type inflight_terminal_direction = terminal_direction_type::kNone;
    // 已进入本地最终状态动作阶段：首次进入时重置 resolve_times，重发/timer 不重置；
    // load 从 running 的 COMMITING 状态恢复本标记，保留已消耗的重试次数。
    bool local_action_stage_entered = false;
    // 等待 wound 的全局决议；快照独立保存此标记，不改变事务状态和原锁归属。
    bool wounded = false;
  };

  /**
   * @brief Get all running transactions
   *
   * @return running transactions
   */
  DISTRIBUTED_TRANSACTION_SDK_API const std::unordered_map<std::string, running_transaction_entry>&
  get_running_transactions() const noexcept;

  /**
   * @brief Get all finished transactions which are not removed yet
   *
   * @return all finished transactions which are not removed yet
   */
  DISTRIBUTED_TRANSACTION_SDK_API const std::unordered_map<std::string, storage_ptr_type>& get_finished_transactions()
      const noexcept;

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  using resolve_custom_timer_watcher_for_unit_test_type = atfw::util::time::jiffies_timer<8, 3, 9>::timer_wptr_t;

  /**
   * @brief 单测 seam：是否注册了 resolve 自定义定时器，及其指向的到期时间点
   * @note 仅用于单元测试验证“每个 handle 至多一个定时器、指向最先发生的事件”的不变量。
   *       仅在启用 PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS 的构建中可用。
   */
  DISTRIBUTED_TRANSACTION_SDK_API bool has_resolve_custom_timer_for_unit_test() const noexcept;
  DISTRIBUTED_TRANSACTION_SDK_API atfw::util::time::time_utility::raw_time_t
  get_resolve_custom_timer_timepoint_for_unit_test() const noexcept;
  DISTRIBUTED_TRANSACTION_SDK_API resolve_custom_timer_watcher_for_unit_test_type
  get_resolve_custom_timer_watcher_for_unit_test() const noexcept;
  /**
   * @brief 单测 seam：模拟已注册的自定义定时器在指定业务时间触发。
   * @note 用于构造“定时器触发时 resolve task 正在运行”的确定性并发场景；会先撤销真实定时器。
   */
  DISTRIBUTED_TRANSACTION_SDK_API void fire_resolve_custom_timer_for_unit_test(
      atfw::util::time::time_utility::raw_time_t timepoint);
#endif

 private:
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_running_transcation(rpc::context& ctx, storage_type&& storage,
                                                                             storage_ptr_type& output);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_running_transaction(
      rpc::context& ctx, EnDistibutedTransactionStatus target_status, const std::string& transaction_uuid,
      storage_ptr_type* output = nullptr);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type start_finished_transaction(
      rpc::context& ctx, const storage_ptr_type& transaction_ptr);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_finished_transaction(
      rpc::context& ctx, const storage_ptr_type& transaction_ptr);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type resolve_transcation(rpc::context& ctx,
                                                                         const std::string& transaction_uuid);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type commit_transcation(rpc::context& ctx,
                                                                        const std::string& transaction_uuid);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_transcation(rpc::context& ctx,
                                                                        const std::string& transaction_uuid);

 private:
  friend class task_action_participator_resolve_transaction;

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_finished_transaction_result(
      rpc::context& ctx, const storage_ptr_type& transaction_ptr, int32_t result, const metadata_type& metadata);

  // running 阶段查询协调者并执行本地动作；finished 阶段完成 on_finished 后确认协调者。
  // 事务同一时刻只会处于其中一种阶段，因此同一 UUID 至多一条 timer，由 action 区分行为
  enum class resolve_timer_action_type : int32_t {
    kQuery = 0,
    kAcknowledge = 1,
  };

  bool is_current_transaction(resolve_timer_action_type action, const storage_ptr_type& storage) const noexcept;

  void retry_resolve_transaction(resolve_timer_action_type action, const storage_ptr_type& storage,
                                 bool writable_check_failed);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type finish_resolve_task(rpc::context& ctx);

  struct storage_resolve_timer_type {
    inline explicit storage_resolve_timer_type(resolve_timer_action_type act, const storage_type& storage)
        : action(act),
          timepoint{protobuf_to_system_clock(storage.resolve_timepoint())},
          transaction_uuid(storage.metadata().transaction_uuid()) {}

    // action 不参与排序：(timepoint, uuid) 已唯一确定条目（uuid 在队列中唯一），uuid 兼作确定性 tie-break
    inline friend bool operator<(const storage_resolve_timer_type& l, const storage_resolve_timer_type& r) noexcept {
      if (l.timepoint != r.timepoint) {
        return l.timepoint < r.timepoint;
      }

      return l.transaction_uuid < r.transaction_uuid;
    }

    inline friend bool operator==(const storage_resolve_timer_type& l, const storage_resolve_timer_type& r) noexcept {
      return l.action == r.action && l.timepoint == r.timepoint && l.transaction_uuid == r.transaction_uuid;
    }

    inline friend bool operator!=(const storage_resolve_timer_type& l, const storage_resolve_timer_type& r) noexcept {
      return l.action != r.action || l.timepoint != r.timepoint || l.transaction_uuid != r.transaction_uuid;
    }

    resolve_timer_action_type action;
    atfw::util::time::time_utility::raw_time_t timepoint;
    std::string transaction_uuid;
  };

  // 截止时间队列：(timepoint, uuid) 有序集合 + uuid 唯一索引。insert/erase 必须同时维护两者，
  // 封装为单一对象后不再暴露分离的容器，消除两者之间的隐式同步关系
  struct resolve_timer_queue_type {
    // 同一 UUID 至多一条 timer：insert 会先替换同 UUID 的旧 timer（含跨阶段残留），再按到期时间入队
    DISTRIBUTED_TRANSACTION_SDK_API void insert_or_replace(resolve_timer_action_type action,
                                                           const storage_type& storage);
    DISTRIBUTED_TRANSACTION_SDK_API void erase(const std::string& transaction_uuid);
    DISTRIBUTED_TRANSACTION_SDK_API void clear();

    inline bool empty() const noexcept { return timers_.empty(); }

    // 触发所有的已到期定时器，自动移除并触发回调。
    // 契约：timer 在回调【之前】即从队列移除（防止处理方漏移除导致重复触发），处理方取得该条目的
    // 恢复所有权——处理失败（如任务拉起失败/任务异常退出）时必须由处理方重新 insert_or_replace；
    // fn 返回 false 停止遍历（当前 timer 已移除，剩余 timer 保留）；
    // fn 内不得插入已到期的 timer，否则会在本次调用中被再次触发。
    DISTRIBUTED_TRANSACTION_SDK_API void trigger_due(
        atfw::util::time::time_utility::raw_time_t timepoint,
        ::atfw::util::nostd::function_ref<bool(const storage_resolve_timer_type& timer)> fn);

    // 最早到期事件（无则返回 nullptr）：用于把 handle 的自定义定时器指向最先发生的事件。
    // 返回指针仅在下一次队列变更前有效，调用方须立即读取。
    inline const storage_resolve_timer_type* earliest() const noexcept {
      return timers_.empty() ? nullptr : &*timers_.begin();
    }

    // 队列内容（最早到期事件可能）变化后的通知钩子：由 handle 构造函数设置，用于维护至多一个、
    // 指向最早到期事件的 atapp 自定义定时器。在 insert_or_replace/erase/clear/trigger_due 完成后触发。
    // 裸函数指针 + 显式传入 handle，避免 lambda 捕获和 std::function 分配
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    void (*on_change)(transaction_participator_handle*) = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    transaction_participator_handle* on_change_handle = nullptr;

   private:
    std::set<storage_resolve_timer_type> timers_;
    std::unordered_map<std::string, storage_resolve_timer_type> index_;
  };

  // 重新排期重试定时器：按 resolve_retry_interval 退避后重新入队。
  // 不修改重试次数，由调用方负责计数和耗尽后的清理。
  void schedule_resolve_retry(resolve_timer_action_type action, storage_type& storage);

  // 与 atapp 自定义定时器一致的时间轮类型（atframe/atapp_common_types.h 的 jiffies_timer_t）
  using atapp_timer_t = atfw::util::time::jiffies_timer<8, 3, 9>;

  // resolve_timer_queue_ 的变更钩子：队列内容变化时把自定义定时器重新指向最早的到期事件，
  // 保证每个 handle 至多注册一个 atapp 自定义定时器；定时器到期自动驱动 tick()，
  // finished/running 事务的恢复流程不再依赖外部周期调用 tick()。
  // 无 atapp 实例的嵌入场景（get_last_instance() 为 nullptr）保持原有外部 tick() 驱动契约。
  void refresh_resolve_custom_timer();
  void on_resolve_custom_timer_fired();
  // resolve_timer_queue_.on_change 的入口：静态成员函数保证其指针是普通函数指针，
  // handle 由参数传入而非捕获
  static void on_resolve_timer_queue_changed(transaction_participator_handle* self);
  // 撤销已注册的自定义定时器（watcher 失效时为空操作）；静态 remove_timer 直接按属主时间轮摘除，
  // 不依赖 atapp::app::get_last_instance()
  void remove_resolve_custom_timer() noexcept;

  void* private_data_;
  on_destroy_callback_type on_destroy_;

  std::string participator_key_;
  atfw::util::memory::strong_rc_ptr<vtable_type> vtable_;
  resolve_timer_queue_type resolve_timer_queue_;

  // 已注册的 atapp 自定义定时器 watcher（失效即表示无等待中的定时器）及其指向的到期时间点；
  // 时间点用于在最早事件未变化时跳过重注册，避免 load 等批量操作反复注销重建
  atapp_timer_t::timer_wptr_t resolve_custom_timer_watcher_;
  atfw::util::time::time_utility::raw_time_t resolve_custom_timer_timepoint_{};

  std::unordered_map<std::string, running_transaction_entry> running_transactions_;
  std::unordered_map<std::string, storage_ptr_type> transaction_locks_;
  std::unordered_map<std::string, storage_ptr_type> finished_transactions_;

  task_type_trait::task_type auto_resolve_transaction_task_;
};

}  // namespace distributed_system
}  // namespace atframework
