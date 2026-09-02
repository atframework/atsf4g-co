// Copyright 2026 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/match_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

class task_action_matching_heart_bear
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingCheckReq,
                                     PROJECT_NAMESPACE_ID::SSMatchingSnapshot> {
 public:
  using base_type =
      task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingCheckReq, PROJECT_NAMESPACE_ID::SSMatchingSnapshot>;
  using base_type::operator();

  explicit task_action_matching_heart_bear(dispatcher_start_data_type&& param);
  ~task_action_matching_heart_bear() override;

  const char* name() const override;
  result_type operator()() override;
  int on_success() override;
  int on_failed() override;
};
