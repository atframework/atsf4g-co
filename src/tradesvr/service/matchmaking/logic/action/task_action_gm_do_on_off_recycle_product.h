// Copyright 2026 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/trade_matchmaking_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

namespace handle {
namespace trade_matchmaking {
class task_action_gm_do_on_off_recycle_product
    : public task_action_ss_rpc_base<atframework::trade::SSMatchMakingDoOnOffRecycleProductReq,
                                     atframework::trade::SSMatchMakingDoOnOffRecycleProductRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::trade::SSMatchMakingDoOnOffRecycleProductReq,
                                            atframework::trade::SSMatchMakingDoOnOffRecycleProductRsp>;
  using base_type::base_type;
  using base_type::operator();

  const char* name() const override;
  result_type operator()() override;
};
}  // namespace trade_matchmaking
}  // namespace handle
