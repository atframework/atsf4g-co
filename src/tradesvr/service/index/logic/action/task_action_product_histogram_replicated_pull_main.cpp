// Copyright 2026 atframework

#include "logic/action/task_action_product_histogram_replicated_pull_main.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_index {
const char* task_action_product_histogram_replicated_pull_main::name() const {
  return "task_action_product_histogram_replicated_pull_main";
}

task_action_product_histogram_replicated_pull_main::result_type
task_action_product_histogram_replicated_pull_main::operator()() {
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_index
}  // namespace handle
