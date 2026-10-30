// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstdint>

#include <std/explicit_declare.h>

#include <rpc/rpc_utils.h>

PROJECT_NAMESPACE_BEGIN
class DMailContent;
class DMailUserInfo;
PROJECT_NAMESPACE_END

namespace rpc {
namespace mail {
/**
 * @param zone_id 0则是跨区服全服邮件
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type add_global_mail(::rpc::context &ctx,
                                                                            PROJECT_NAMESPACE_ID::DMailContent &mail,
                                                                            uint32_t zone_id, int32_t channel,
                                                                            int64_t channel_param);

/**
 * @param zone_id 0则是跨区服全服邮件
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type add_global_mail_with_template(
    ::rpc::context &ctx, int32_t mail_template_id, const PROJECT_NAMESPACE_ID::DMailUserInfo *sender, uint32_t zone_id,
    int32_t channel, int64_t channel_param, bool render_by_client);

/**
 * @param zone_id 必须正确填大区ID，0则是跨区服全服邮件
 * @param mail_id 邮件ID
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type remove_global_mail(::rpc::context &ctx, uint32_t zone_id,
                                                                               int64_t mail_id);
}  // namespace mail
}  // namespace rpc
