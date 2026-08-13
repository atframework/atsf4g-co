#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.rank.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_cs_req_base.h>



class task_action_rank_get_specify_rank
    : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSRankGetSpecifyRankReq, PROJECT_NAMESPACE_ID::SCRankGetSpecifyRankRsp> {
 public:
  using base_type = task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSRankGetSpecifyRankReq, PROJECT_NAMESPACE_ID::SCRankGetSpecifyRankRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  explicit task_action_rank_get_specify_rank(dispatcher_start_data_type&& param);
  ~task_action_rank_get_specify_rank();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
