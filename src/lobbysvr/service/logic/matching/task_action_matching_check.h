// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.match.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/task_action_cs_req_base.h>



class task_action_matching_check
    : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSMatchingCheckReq,
                                     PROJECT_NAMESPACE_ID::SCMatchingCheckRsp> {
 public:
  using base_type = task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSMatchingCheckReq,
                                            PROJECT_NAMESPACE_ID::SCMatchingCheckRsp>;
  using base_type::operator();

  explicit task_action_matching_check(dispatcher_start_data_type&& param);
  ~task_action_matching_check() override;
  const char* name() const override;
  result_type operator()() override;
  int on_success() override;
  int on_failed() override;
};
