// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#include "rpc/game/player.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <rpc/db/uuid.h>

#include "rpc/rpc_utils.h"

namespace rpc {
namespace game {
namespace player {
rpc::rpc_result<int64_t> alloc_user_id(::rpc::context &ctx) {
  int64_t prefix_id = RPC_AWAIT_TYPE_RESULT(
      rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 0, 0));
  if (prefix_id < 0) {
    RPC_RETURN_CODE(static_cast<int>(prefix_id));
  }

  int64_t suffix = prefix_id;
  while (suffix >= 8) {
    suffix = (suffix >> 3) ^ (suffix & 0x07);
  }

  int64_t out = (static_cast<uint64_t>(prefix_id) << 3) | static_cast<uint64_t>(suffix);
  assert(is_valid_user_id(out));
  RPC_RETURN_CODE(out);
}

bool is_valid_user_id(int64_t in) {
  if (in <= 0) {
    return false;
  }

  while (in >= 8) {
    in = (in >> 3) ^ (in & 0x07);
  }

  return in == 0;
}

}  // namespace player
}  // namespace game
}  // namespace rpc