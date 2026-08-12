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

SERVER_FRAME_API void merge_basic_profile(PROJECT_NAMESPACE_ID::DUserBasicData& /*output*/,
                                          const PROJECT_NAMESPACE_ID::DUserCacheMetaBasicProfile& /*input*/) noexcept {
  // 填充基本信息
}

SERVER_FRAME_API void convert_to_client_data(PROJECT_NAMESPACE_ID::DLoginBasicDataCache& output,
                                             const PROJECT_NAMESPACE_ID::table_user& input_user) noexcept {
  const auto& login_data = input_user.login_data();
  output.set_business_register_time(login_data.business_register_time());
  output.set_business_login_time(login_data.business_login_time());
  output.set_business_logout_time(login_data.business_logout_time());
  output.set_business_unregister_time(login_data.business_unregister_time());
}

SERVER_FRAME_API void convert_to_client_data(PROJECT_NAMESPACE_ID::DUserBasicData& output,
                                             const PROJECT_NAMESPACE_ID::table_user& input) noexcept {
  output.mutable_user_key()->set_zone_id(input.zone_id());
  output.mutable_user_key()->set_user_id(input.user_id());

  output.set_account_type(input.account_data().account_type());
  output.set_account_login_channel(input.account_data().channel_id());
  // output.set_account_id(0);

  convert_to_client_data(*output.mutable_login_data_cache(), input);

  // output.set_client_version();
  protobuf_copy_message(*output.mutable_profile(), input.account_data().profile());
  // output.set_package_register_channel();
  // output.set_package_login_channel();
  output.set_version_type(input.account_data().version_type());

  output.set_user_level(input.user_data().user_level());
  output.set_user_data_version(input.data_version());

  protobuf_copy_message(*output.mutable_shared_options(), input.option_data_public().custom_options());
}

}  // namespace user
}  // namespace rpc
