// Copyright 2022 atframework
// @brief Created by owent on 2022-03-01 11:45:04

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/task_action_no_req_base.h>

#include <memory/rc_ptr.h>
#include <list>
#include <string>
#include <vector>

namespace atframework {
namespace distributed_system {

class transaction_participator_handle;

class task_action_participator_resolve_transaction : public task_action_no_req_base {
 public:
  using storage_type = atfw::distributed_system::transaction_participator_storage;
  using storage_ptr_type = atfw::util::memory::strong_rc_ptr<storage_type>;

  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    atfw::util::memory::strong_rc_ptr<transaction_participator_handle> participantor;
    std::list<std::string> pending_transactions;
    std::vector<storage_ptr_type> submmit_transactions;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  DISTRIBUTED_TRANSACTION_SDK_API explicit task_action_participator_resolve_transaction(ctor_param_t&& param);
  DISTRIBUTED_TRANSACTION_SDK_API ~task_action_participator_resolve_transaction();

  DISTRIBUTED_TRANSACTION_SDK_API const char* name() const override;

  DISTRIBUTED_TRANSACTION_SDK_API result_type operator()() override;

  DISTRIBUTED_TRANSACTION_SDK_API int on_success() override;
  DISTRIBUTED_TRANSACTION_SDK_API int on_failed() override;

 private:
  // trigger_due 在拉起本任务前已移除所有待处理条目的 timer，恢复所有权随任务转移。
  // 已处理条目由 resolve_transcation/handle_finished_transaction_result 自行重排或消费；
  // 任务异常退出（不可写/exiting/被 kill/超时）时为未处理条目重新武装 timer，避免恢复流程永久丢失。
  // 幂等（内部 rearm_done_ 去重），在 operator() 正常收尾和 on_failed 中都会调用
  void rearm_unprocessed_timers();

 private:
  ctor_param_t param_;
  size_t submmit_processed_ = 0;
  std::list<std::string>::iterator pending_iter_;
  bool rearm_done_ = false;
};

}  // namespace distributed_system
}  // namespace atframework
