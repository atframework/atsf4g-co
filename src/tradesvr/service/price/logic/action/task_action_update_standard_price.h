// Copyright 2026 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/trade_price_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

namespace handle {
namespace trade_price {
class task_action_update_standard_price
    : public task_action_ss_rpc_base<atframework::trade::SSTradePriceUpdateStandardPriceReq,
                                     atframework::trade::SSTradePriceUpdateStandardPriceRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::trade::SSTradePriceUpdateStandardPriceReq,
                                            atframework::trade::SSTradePriceUpdateStandardPriceRsp>;
  using base_type::base_type;
  using base_type::operator();

  const char* name() const override;
  result_type operator()() override;
};
}  // namespace trade_price
}  // namespace handle
