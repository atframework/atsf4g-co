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

#ifndef GAMECLIENT_SERVICE_API
#  define GAMECLIENT_SERVICE_API UTIL_SYMBOL_VISIBLE
#endif

class task_action_rank_get_self_rank
    : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSRankGetSelfRankReq, PROJECT_NAMESPACE_ID::SCRankGetSelfRankRsp> {
 public:
  using base_type = task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSRankGetSelfRankReq, PROJECT_NAMESPACE_ID::SCRankGetSelfRankRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  GAMECLIENT_SERVICE_API explicit task_action_rank_get_self_rank(dispatcher_start_data_type&& param);
  GAMECLIENT_SERVICE_API ~task_action_rank_get_self_rank();

  GAMECLIENT_SERVICE_API const char* name() const override;

  GAMECLIENT_SERVICE_API result_type operator()() override;

  GAMECLIENT_SERVICE_API int on_success() override;
  GAMECLIENT_SERVICE_API int on_failed() override;
};
