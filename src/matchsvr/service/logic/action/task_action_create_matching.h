// Copyright 2026 atframework

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_create_matching
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingCreateReq,
                                     PROJECT_NAMESPACE_ID::SSMatchingSnapshot> {
 public:
  using base_type =
      task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingCreateReq, PROJECT_NAMESPACE_ID::SSMatchingSnapshot>;
  using base_type::operator();

  explicit task_action_create_matching(dispatcher_start_data_type&& param);
  ~task_action_create_matching() override;

  const char* name() const override;
  result_type operator()() override;
  int on_success() override;
  int on_failed() override;
};
