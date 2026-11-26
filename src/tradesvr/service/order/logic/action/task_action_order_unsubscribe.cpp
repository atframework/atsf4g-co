// Copyright 2026 atframework

#include "logic/action/task_action_order_unsubscribe.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_order {
const char* task_action_order_unsubscribe::name() const { return "task_action_order_unsubscribe"; }

task_action_order_unsubscribe::result_type task_action_order_unsubscribe::operator()() {
  disable_response_message();
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_order
}  // namespace handle
