// Copyright 2026 atframework
// Created by owent on 2026/04/28.
//

#include "rpc/user/user_basic.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <rpc/db/uuid.h>

namespace rpc {
namespace user {
SERVER_FRAME_API rpc::rpc_result<int64_t> allocate_user_id(::rpc::context& ctx) {
  int64_t prefix_id = RPC_AWAIT_TYPE_RESULT(
      rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 0, 0));
  if (prefix_id < 0) {
    RPC_RETURN_CODE(static_cast<int>(prefix_id));
  }

  int64_t suffix = prefix_id;
  while (suffix >= 8) {
    suffix = (suffix >> 3) ^ (suffix & 0x07);
  }

  int64_t out = static_cast<int64_t>((static_cast<uint64_t>(prefix_id) << 3) | static_cast<uint64_t>(suffix));
  assert(is_valid_user_id(out));
  RPC_RETURN_CODE(out);
}

SERVER_FRAME_API bool is_valid_user_id(int64_t in) noexcept {
  if (in <= 0) {
    return false;
  }

  while (in >= 8) {
    in = (in >> 3) ^ (in & 0x07);
  }

  return in == 0;
}

SERVER_FRAME_API void convert_to_client_meta_data(PROJECT_NAMESPACE_ID::DUserBasicDataMeta& output,
                                                  const PROJECT_NAMESPACE_ID::table_user& input_user) noexcept {
  output.mutable_user_key()->set_zone_id(input_user.zone_id());
  output.mutable_user_key()->set_user_id(input_user.user_id());

  output.mutable_login_data()->set_business_register_time(input_user.login_data().business_register_time());
  output.mutable_login_data()->set_business_login_time(input_user.login_data().business_login_time());
  output.mutable_login_data()->set_business_logout_time(input_user.login_data().business_logout_time());
  output.mutable_login_data()->set_business_unregister_time(input_user.login_data().business_unregister_time());

  output.mutable_profile()->set_open_id(input_user.account_data().profile().open_id());

  output.set_user_data_version(input_user.data_version());
}

SERVER_FRAME_API void convert_to_client_data(PROJECT_NAMESPACE_ID::DUserBasicData& output,
                                             const PROJECT_NAMESPACE_ID::table_user& input) noexcept {
  convert_to_client_meta_data(*output.mutable_meta_data(), input);
  protobuf_copy_message(*output.mutable_shared_options(), input.option_data_public().custom_options());
  protobuf_copy_message(*output.mutable_basic_cache(), input.user_data().basic_cache());
}

}  // namespace user
}  // namespace rpc
