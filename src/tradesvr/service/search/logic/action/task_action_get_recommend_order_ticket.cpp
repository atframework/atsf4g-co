// Copyright 2026 atframework

#include "logic/action/task_action_get_recommend_order_ticket.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_search {
const char* task_action_get_recommend_order_ticket::name() const { return "task_action_get_recommend_order_ticket"; }

task_action_get_recommend_order_ticket::result_type task_action_get_recommend_order_ticket::operator()() {
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_search
}  // namespace handle
