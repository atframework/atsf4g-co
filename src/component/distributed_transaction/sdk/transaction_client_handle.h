// Copyright 2022 atframework
// Created by owent, on 2022-03-03

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

namespace rpc {
class context;
}

#include <memory/rc_ptr.h>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_set>

namespace atframework {
namespace distributed_system {

class transaction_client_handle {
 public:
  using storage_type = atfw::distributed_system::transaction_blob_storage;
  using metadata_type = atfw::distributed_system::transaction_metadata;
  using configure_type = atfw::distributed_system::transaction_configure;
  using storage_ptr_type = atfw::util::memory::strong_rc_ptr<storage_type>;
  using storage_const_ptr_type = atfw::util::memory::strong_rc_ptr<const storage_type>;
  using participator_type = atfw::distributed_system::transaction_participator;
  using transaction_participator_failure_reason = atfw::distributed_system::transaction_participator_failure_reason;

  struct ATFW_UTIL_SYMBOL_VISIBLE vtable_type {
    std::function<rpc::result_code_type(rpc::context&, transaction_client_handle&, const storage_type&,
                                        const participator_type&, transaction_participator_failure_reason&)>
        prepare_participator;

    // 最终状态通知回调。必须 await 并返回真实的投递/响应结果：返回码用于判定通知是否成功并驱动有限次重试，
    // 不能 fire-and-forget 后谎报成功。不同参与者按顺序通知，同一参与者内部保持有限次重试。
    std::function<rpc::result_code_type(rpc::context&, transaction_client_handle&, const storage_type&,
                                        const participator_type&)>
        commit_participator;

    // 同 commit_participator：必须 await 并返回真实的投递/响应结果。
    std::function<rpc::result_code_type(rpc::context&, transaction_client_handle&, const storage_type&,
                                        const participator_type&)>
        reject_participator;
  };
  using on_destroy_callback_type = void (*)(transaction_client_handle*);

  struct ATFW_UTIL_SYMBOL_VISIBLE transaction_options {
    uint32_t replication_read_count = 0;
    uint32_t replication_total_count = 0;
    bool memory_only = false;

    // 是否直接强制提交（并执行）事务。
    // 注意：force_commit 是 best-effort 模型，不是可容灾 2PC：
    // - 不创建协调者记录，参与者不进入 running/finished，也没有定时恢复；
    // - SDK 资源锁对 force_commit 事务不生效；
    // - 补偿（undo）只存在于本次 submit 调用的有限次重试内，client/参与者故障可能永久部分执行；
    // - 参与者的 do_event/undo_event 必须幂等，undo_event 必须支持 no-op（未执行过时成功返回）和重放。
    bool force_commit = false;
    std::chrono::system_clock::duration timeout = std::chrono::seconds(5);

    uint32_t resolve_max_times = 3;
    uint32_t lock_retry_max_times = 5;
    std::chrono::system_clock::duration resolve_retry_interval = std::chrono::seconds(10);
    std::chrono::system_clock::duration lock_wait_interval_min = std::chrono::milliseconds(32);
    std::chrono::system_clock::duration lock_wait_interval_max = std::chrono::milliseconds(256);

    // Patch BUGs of GCC and Clang
    // @see
    // https://stackoverflow.com/questions/53408962/try-to-understand-compiler-error-message-default-member-initializer-required-be
    // @see https://bugs.llvm.org/show_bug.cgi?id=36684
    inline transaction_options() {}
  };

 public:
  transaction_client_handle(const transaction_client_handle&) = delete;
  transaction_client_handle(transaction_client_handle&&) = delete;
  transaction_client_handle& operator=(const transaction_client_handle&) = delete;
  transaction_client_handle& operator=(transaction_client_handle&&) = delete;

  DISTRIBUTED_TRANSACTION_SDK_API transaction_client_handle(
      const atfw::util::memory::strong_rc_ptr<vtable_type>& vtable);
  DISTRIBUTED_TRANSACTION_SDK_API ~transaction_client_handle();

  ATFW_UTIL_FORCEINLINE void* get_private_data() const noexcept { return private_data_; }
  ATFW_UTIL_FORCEINLINE void set_private_data(void* ptr) noexcept { private_data_ = ptr; }
  ATFW_UTIL_FORCEINLINE on_destroy_callback_type get_on_destroy_callback() const noexcept { return on_destroy_; }
  ATFW_UTIL_FORCEINLINE void set_on_destroy_callback(on_destroy_callback_type fn) noexcept { on_destroy_ = fn; }

  /**
   * @brief Create a transaction object
   *
   * @param ctx RPC context
   * @param output output the created transaction object
   * @param timeout 超时时间
   * @param replication_read_count 一致性模型: 读副本数
   *
   * @note 当前一致性模型采用依赖数据库CAS操作或Read-your-writes模型。
   *       当replication_read_count=0且replication_total_count=0时，事务系统依赖数据库CAS操作来实现一致性保证。
   *       当replication_read_count>0且replication_total_count>=当replication_read_count时，事务系统采用Read-your-writes模型。
   *       此时replication_read_count为Read-your-writes的读参数(R),replication_total_count为Read-your-writes的总副本数(N)。
   *       如果事务协调者服务进程数少于replication_total_count，则replication_total_count会自动调整为协调者服务进程数。
   *
   * @see http://www.dbms2.com/2010/05/01/ryw-read-your-writes-consistency/
   * @see https://en.wikipedia.org/wiki/Consistency_model#Client-centric_consistency_models
   *
   * @return future of 0 or error code
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type create_transaction(
      rpc::context& ctx, storage_ptr_type& output, const transaction_options& options = {});

  /**
   * @brief 执行事务
   *
   * @param ctx RPC context
   * @param input 事务存储结构
   * @param output_prepared_participators 输出prepare阶段完成的参与者
   * @param output_failed_participators 输出失败的参与者。仅包含 prepare 阶段失败（导致事务被拒绝）的参与者；
   *   最终状态通知投递失败的参与者不在其中——该投递由参与者的 resolve 重试流程保证最终一致，
   *   对 client 而言这些参与者的事务执行视为成功
   * @return future of 0 or error code
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type submit_transaction(
      rpc::context& ctx, storage_ptr_type& input,
      std::unordered_set<std::string>* output_prepared_participators = nullptr,
      std::unordered_set<std::string>* output_failed_participators = nullptr);

  DISTRIBUTED_TRANSACTION_SDK_API int32_t set_transaction_data(rpc::context& ctx, storage_ptr_type& input,
                                                               google::protobuf::Message& data);

  DISTRIBUTED_TRANSACTION_SDK_API int32_t add_participator(rpc::context& ctx, storage_ptr_type& input,
                                                           const std::string& participator_key,
                                                           google::protobuf::Message& data);

 private:
  void* private_data_;
  on_destroy_callback_type on_destroy_;
  atfw::util::memory::strong_rc_ptr<vtable_type> vtable_;
};

}  // namespace distributed_system
}  // namespace atframework
