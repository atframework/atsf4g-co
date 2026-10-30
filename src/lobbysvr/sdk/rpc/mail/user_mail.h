// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <string>

#include <std/explicit_declare.h>

#include <rpc/rpc_utils.h>

PROJECT_NAMESPACE_BEGIN
class DMailContent;
class DMailUserInfo;
PROJECT_NAMESPACE_END

namespace rpc {
namespace mail {
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type add_user_mail(::rpc::context &ctx, uint64_t user_id,
                                                                          uint32_t zone_id,
                                                                          PROJECT_NAMESPACE_ID::DMailContent &&mail);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type
add_user_mail_with_template(::rpc::context &ctx, int32_t mail_template_id, uint32_t zone_id,
                            const PROJECT_NAMESPACE_ID::DMailUserInfo &receiver,
                            const PROJECT_NAMESPACE_ID::DMailUserInfo *sender, bool render_by_client);
}  // namespace mail
}  // namespace rpc
