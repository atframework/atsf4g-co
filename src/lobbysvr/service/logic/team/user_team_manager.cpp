// Copyright 2026 atframework

#include "logic/team/user_team_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_context.h>

#include "logic/chat/user_chat_manager.h"

namespace {
static void dispatch_team_member_event(rpc::context& /*ctx*/, user&, const ::atfw::dtmq::DChannelMessage& /*data*/) {
  // TODO(owent): 处理队伍成员事件
}
}  // namespace

user_team_manager::user_team_manager(user& owner) : owner_(&owner) {}

user_team_manager::~user_team_manager() {}

rpc::result_code_type user_team_manager::login_init(rpc::context&) {
  user_chat_manager::global_setup_private_channel_event_callback<::atfw::team::DTeamMemberAction>(
      reinterpret_cast<uintptr_t>(dispatch_team_member_event), dispatch_team_member_event);
  RPC_RETURN_CODE(0);
}
