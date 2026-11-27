// Copyright 2026 atframework

#include "logic/action/task_action_fetch_product_price_permille.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_index {
const char* task_action_fetch_product_price_permille::name() const {
  return "task_action_fetch_product_price_permille";
}

task_action_fetch_product_price_permille::result_type task_action_fetch_product_price_permille::operator()() {
  set_response_code(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_index
}  // namespace handle
