// Copyright 2026 atframework

#include "logic/action/task_action_fetch_product_price_percentage.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_index {
const char* task_action_fetch_product_price_percentage::name() const {
  return "task_action_fetch_product_price_percentage";
}

task_action_fetch_product_price_percentage::result_type task_action_fetch_product_price_percentage::operator()() {
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_index
}  // namespace handle
