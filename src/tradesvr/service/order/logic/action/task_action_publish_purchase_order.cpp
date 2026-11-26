// Copyright 2026 atframework

#include "logic/action/task_action_publish_purchase_order.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_order {
const char* task_action_publish_purchase_order::name() const { return "task_action_publish_purchase_order"; }

task_action_publish_purchase_order::result_type task_action_publish_purchase_order::operator()() {
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_order
}  // namespace handle
