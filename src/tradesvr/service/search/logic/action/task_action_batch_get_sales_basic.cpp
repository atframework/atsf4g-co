// Copyright 2026 atframework

#include "logic/action/task_action_batch_get_sales_basic.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_search {
const char* task_action_batch_get_sales_basic::name() const { return "task_action_batch_get_sales_basic"; }

task_action_batch_get_sales_basic::result_type task_action_batch_get_sales_basic::operator()() {
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_search
}  // namespace handle
