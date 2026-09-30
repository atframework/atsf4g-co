// Copyright 2026 atframework
// Created by owent on 2026-09-28

#pragma once

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

PROJECT_NAMESPACE_BEGIN
// class table_friend;
class table_user;
PROJECT_NAMESPACE_END

namespace atframework {
namespace friend_api {
class table_friend_blob_data;

class DFriendInvitationInfo;
class DFriendStatistics;
class DFriendEvent;
class DFriendGiftHistory;
class DFriendGift;

class DFriendManagementNotificationEvent;
}  // namespace friend_api
}  // namespace atframework

namespace rpc {
class context;
}
