// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <vector>

namespace rpc {
class context;
}

namespace atframework {
namespace mail {

class task_action_batch_remove_mail_content : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::vector<int64_t> mail_ids;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  LOBBY_RPC_API explicit task_action_batch_remove_mail_content(ctor_param_t&& param);
  LOBBY_RPC_API ~task_action_batch_remove_mail_content();

  LOBBY_RPC_API const char* name() const override;

  LOBBY_RPC_API result_type operator()() override;

  LOBBY_RPC_API int on_success() override;
  LOBBY_RPC_API int on_failed() override;

  LOBBY_RPC_API static int create_and_start(rpc::context& ctx, std::vector<int64_t>&& mail_ids);

 private:
  ctor_param_t param_;
};
}  // namespace mail
}  // namespace atframework
