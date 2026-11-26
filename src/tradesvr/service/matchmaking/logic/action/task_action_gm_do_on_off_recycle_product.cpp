// Copyright 2026 atframework

#include "logic/action/task_action_gm_do_on_off_recycle_product.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_matchmaking {
const char* task_action_gm_do_on_off_recycle_product::name() const {
  return "task_action_gm_do_on_off_recycle_product";
}

task_action_gm_do_on_off_recycle_product::result_type task_action_gm_do_on_off_recycle_product::operator()() {
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_matchmaking
}  // namespace handle
