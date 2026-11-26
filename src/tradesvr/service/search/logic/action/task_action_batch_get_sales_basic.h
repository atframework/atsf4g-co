// Copyright 2026 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/trade_search_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

namespace handle {
namespace trade_search {
class task_action_batch_get_sales_basic
    : public task_action_ss_rpc_base<atframework::trade::SSTradeSearchBatchGetSalesBasicIndexReq,
                                     atframework::trade::SSTradeSearchBatchGetSalesBasicIndexRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::trade::SSTradeSearchBatchGetSalesBasicIndexReq,
                                            atframework::trade::SSTradeSearchBatchGetSalesBasicIndexRsp>;
  using base_type::base_type;
  using base_type::operator();

  const char* name() const override;
  result_type operator()() override;
};
}  // namespace trade_search
}  // namespace handle
