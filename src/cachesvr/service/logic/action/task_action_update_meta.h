// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-11-03 23:57:58

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/cache_service.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

class task_action_update_meta
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSCacheUpdateMetaSync, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSCacheUpdateMetaSync, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_update_meta(dispatcher_start_data_type&& param);
  ~task_action_update_meta();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
