// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-11-27 22:13:49

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/trade_index_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API
#  define ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_fetch_product_price_permille
    : public task_action_ss_rpc_base<atframework::trade::SSTradeGetProductPermilleReq,
                                     atframework::trade::SSTradeGetProductPermilleRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::trade::SSTradeGetProductPermilleReq,
                                            atframework::trade::SSTradeGetProductPermilleRsp>;
  using base_type::message_type;
  using base_type::msg_cref_type;
  using base_type::msg_ref_type;
  using base_type::rpc_request_type;
  using base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API explicit task_action_fetch_product_price_permille(
      dispatcher_start_data_type&& param);
  ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API ~task_action_fetch_product_price_permille() override;

  ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API const char* name() const override;

  ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API result_type operator()() override;

  ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API int on_success() override;
  ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API int on_failed() override;
};
