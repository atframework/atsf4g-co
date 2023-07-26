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
#include <time/time_utility.h>

#include <config/server_frame_build_feature.h>
#include <dispatcher/task_type_traits.h>

#include <rpc/rpc_common_types.h>

#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace rpc {
class context;
}

namespace atframework {
namespace distributed_system {

class task_action_participator_resolve_transaction;

class transaction_participator_handle : public std::enable_shared_from_this<transaction_participator_handle> {
 public:
  using storage_type = atframework::distributed_system::transaction_participator_storage;
  using metadata_type = atframework::distributed_system::transaction_metadata;
  using configure_type = atframework::distributed_system::transaction_configure;
  using snapshot_type = atframework::distributed_system::transaction_participator_snapshot;
  using storage_ptr_type = std::shared_ptr<storage_type>;
  using storage_const_ptr_type = std::shared_ptr<const storage_type>;

  struct UTIL_SYMBOL_VISIBLE vtable_type {
    // 事务执行(Do)回调
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)> do_event;

    // 事务回滚(Undo)回调,仅仅在 force_commit=true 时才会触发
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        undo_event;

    // 执行预检查的回调。
    // 如果允许发起者重试请设置 transaction_participator_failure_reason.allow_retry = true
    // 如果是锁被抢占请 transaction_participator_failure_reason.locked_resource 设置为被抢占的资源,
    //   并且设置 transaction_participator_failure_reason.allow_retry = true
    //   同时返回码设为 0 或者 EN_TRANSACTION_RESOURCE_PREEMPTED
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, storage_type&,
                                        transaction_participator_failure_reason&)>
        check_prepare;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, bool&)> check_writable;

    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_start_running;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_finish_running;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_commited;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_rejected;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&, const storage_type&)>
        on_finished;
    std::function<rpc::result_code_type(rpc::context&, transaction_participator_handle&)> on_resolve_task_finished;
  };

  using on_destroy_callback_type = void (*)(transaction_participator_handle*);

 private:
  transaction_participator_handle(const transaction_participator_handle&) = delete;
  transaction_participator_handle(transaction_participator_handle&&) = delete;
  transaction_participator_handle& operator=(const transaction_participator_handle&) = delete;
  transaction_participator_handle& operator=(transaction_participator_handle&&) = delete;

 public:
  DISTRIBUTED_TRANSACTION_SDK_API transaction_participator_handle(const std::shared_ptr<vtable_type>& vtable,
                                                                  gsl::string_view participator_key);
  DISTRIBUTED_TRANSACTION_SDK_API ~transaction_participator_handle();

  UTIL_FORCEINLINE void* get_private_data() const noexcept { return private_data_; }
  UTIL_FORCEINLINE void set_private_data(void* ptr) noexcept { private_data_ = ptr; }
  UTIL_FORCEINLINE on_destroy_callback_type get_on_destroy_callback() const noexcept { return on_destroy_; }
  UTIL_FORCEINLINE void set_on_destroy_callback(on_destroy_callback_type fn) noexcept { on_destroy_ = fn; };

  UTIL_FORCEINLINE const std::string& get_participator_key() const noexcept { return participator_key_; }

  DISTRIBUTED_TRANSACTION_SDK_API void load(const snapshot_type& storage);

  DISTRIBUTED_TRANSACTION_SDK_API void dump(snapshot_type& storage);

  /**
   * @brief Tick, this function should be called after prepare/commit/reject and interval
   *
   * @param ctx rpc context used to create new task
   * @param timepoint current timepoint
   * @return error code or transaction count to resolve
   */
  DISTRIBUTED_TRANSACTION_SDK_API int32_t tick(rpc::context& ctx, util::time::time_utility::raw_time_t timepoint);

  /**
   * @brief Check writable of current transaction participator
   *
   * @param ctx rpc context used to create new task
   * @param writable output if it's writable now
   * @return future of 0 or error code
   */
EXPLICIT_NODISCARD_ATTR   DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type check_writable(rpc::context& ctx,
                                                                                               bool& writable);

  /**
   * @brief Prepare for transaction
   * @note If force_commit = true, then vtable.do_event(...) will be called immediately
   *   when 0 == vtable.check_prepare(...)
   *
   * @param ctx rpc context
   * @param request RPC request
   * @param response RPC response
   * @param output output the created transaction
   *
   * @return future of 0 or error code
   */
EXPLICIT_NODISCARD_ATTR   DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type prepare(
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
EXPLICIT_NODISCARD_ATTR   DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type commit(
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
EXPLICIT_NODISCARD_ATTR   DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type reject(
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
   * @note We use Wound-Wait to resolve deadlock
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
   * @note We use Wound-Wait to resolve deadlock
   * @see http://www.mathcs.emory.edu/~cheung/Courses/554/Syllabus/8-recv+serial/deadlock-compare.html
   *
   * @return future of 0 or error code
   */
EXPLICIT_NODISCARD_ATTR   DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type lock(
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

  /**
   * @brief Get all running transactions
   *
   * @return running transactions
   */
  DISTRIBUTED_TRANSACTION_SDK_API const std::unordered_map<std::string, storage_ptr_type>& get_running_transactions()
      const noexcept;

  /**
   * @brief Get all finished transactions which are not removed yet
   *
   * @return all finished transactions which are not removed yet
   */
  DISTRIBUTED_TRANSACTION_SDK_API const std::unordered_map<std::string, storage_ptr_type>& get_finished_transactions()
      const noexcept;

 private:
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_running_transcation(rpc::context& ctx, storage_type&& storage,
                                                                        storage_ptr_type& output);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_running_transaction(rpc::context& ctx,
                                                                           EnDistibutedTransactionStatus target_status,
                                                                           const std::string& transaction_uuid,
                                                                           storage_ptr_type* output = nullptr);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_finished_transcation(rpc::context& ctx,
                                                                         const storage_ptr_type& transaction_ptr);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_finished_transaction(rpc::context& ctx,
                                                                            const storage_ptr_type& transaction_ptr);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type resolve_transcation(rpc::context& ctx,
                                                                    const std::string& transaction_uuid);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type commit_transcation(rpc::context& ctx,
                                                                   const std::string& transaction_uuid);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_transcation(rpc::context& ctx,
                                                                   const std::string& transaction_uuid);

 private:
  friend class task_action_participator_resolve_transaction;

  struct storage_resolve_timer_type {
    inline explicit storage_resolve_timer_type(const storage_type& storage)
        : timepoint{std::chrono::system_clock::from_time_t(storage.resolve_timepoint().seconds()) +
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(
                        std::chrono::nanoseconds{storage.resolve_timepoint().nanos()})},
          transaction_uuid(storage.metadata().transaction_uuid()) {}

    inline friend bool operator<(const storage_resolve_timer_type& l, const storage_resolve_timer_type& r) noexcept {
      if (l.timepoint != r.timepoint) {
        return l.timepoint < r.timepoint;
      }

      return l.transaction_uuid < r.transaction_uuid;
    }

    inline friend bool operator==(const storage_resolve_timer_type& l, const storage_resolve_timer_type& r) noexcept {
      return l.timepoint == r.timepoint && l.transaction_uuid == r.transaction_uuid;
    }

    inline friend bool operator!=(const storage_resolve_timer_type& l, const storage_resolve_timer_type& r) noexcept {
      return l.timepoint != r.timepoint || l.transaction_uuid != r.transaction_uuid;
    }

    util::time::time_utility::raw_time_t timepoint;
    std::string transaction_uuid;
  };

  void* private_data_;
  on_destroy_callback_type on_destroy_;

  std::string participator_key_;
  std::shared_ptr<vtable_type> vtable_;
  std::set<storage_resolve_timer_type> resolve_timers_;
  std::unordered_map<std::string, storage_ptr_type> running_transactions_;
  std::unordered_map<std::string, storage_ptr_type> transaction_locks_;
  std::unordered_map<std::string, storage_ptr_type> finished_transactions_;

  task_type_trait::task_type auto_resolve_transaction_task_;
};

}  // namespace distributed_system
}  // namespace atframework
