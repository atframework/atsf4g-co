// Copyright 2026 atframework

#include "logic/action/task_action_query_standard_price_by_group.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace handle {
namespace trade_search {
const char* task_action_query_standard_price_by_group::name() const {
  return "task_action_query_standard_price_by_group";
}

task_action_query_standard_price_by_group::result_type task_action_query_standard_price_by_group::operator()() {
  if (is_stream_rpc()) {
    disable_response_message();
  }
  TASK_ACTION_RETURN_CODE(atframework::shared::EN_ERR_NOT_IMPLEMENTED);
}
}  // namespace trade_search
}  // namespace handle
