// Copyright 2026 atframework

#include "logic/action/task_action_trade_slot_transfer_sync.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_order {
const char* task_action_trade_slot_transfer_sync::name() const { return "task_action_trade_slot_transfer_sync"; }

task_action_trade_slot_transfer_sync::result_type task_action_trade_slot_transfer_sync::operator()() {
  disable_response_message();
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_order
}  // namespace handle
