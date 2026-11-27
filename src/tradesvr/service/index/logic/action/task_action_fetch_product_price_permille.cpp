// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-11-27 22:13:49

#include "logic/action/task_action_fetch_product_price_permille.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/trade_index_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API
task_action_fetch_product_price_permille::task_action_fetch_product_price_permille(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API
    task_action_fetch_product_price_permille::~task_action_fetch_product_price_permille() {}

ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API const char* task_action_fetch_product_price_permille::name() const {
  return "task_action_fetch_product_price_permille";
}

ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API task_action_fetch_product_price_permille::result_type
task_action_fetch_product_price_permille::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API int task_action_fetch_product_price_permille::on_success() {
  return get_result();
}

ATFRAMEWORK_TRADE_TRADEINDEXSERVICE_API int task_action_fetch_product_price_permille::on_failed() {
  return get_result();
}
