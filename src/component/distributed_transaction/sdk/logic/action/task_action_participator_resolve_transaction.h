// Copyright 2022 atframework
// @brief Created by owent on 2022-03-01 11:45:04

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/task_action_no_req_base.h>

#include <memory/rc_ptr.h>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace atframework {
namespace distributed_system {

class transaction_participator_handle;

class task_action_participator_resolve_transaction : public task_action_no_req_base {
 public:
  using storage_type = atframework::distributed_system::transaction_participator_storage;
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
  ctor_param_t param_;
};

}  // namespace distributed_system
}  // namespace atframework
