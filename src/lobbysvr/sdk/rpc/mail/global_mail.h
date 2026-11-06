// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#pragma once

#include <nostd/function_ref.h>

#include <config/server_frame_build_feature.h>

#include <std/explicit_declare.h>

#include <rpc/rpc_utils.h>

#include <cstdint>

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
                                                                            uint32_t zone_id);

/**
 * @brief Add a global mail using a template
 *
 * @param ctx RPC上下文
 * @param mail_template_id 邮件模板ID
 * @param sender 发送者信息
 * @param zone_id 0则是跨区服全服邮件
 * @param render_by_client 是否由客户端渲染
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type add_global_mail_with_template(
    ::rpc::context &ctx, int32_t mail_template_id, const PROJECT_NAMESPACE_ID::DMailUserInfo *sender, uint32_t zone_id,
    bool render_by_client);

/**
 * @brief Add a global mail using a template
 *
 * @param ctx RPC上下文
 * @param mail_template_id 邮件模板ID
 * @param sender 发送者信息
 * @param zone_id 0则是跨区服全服邮件
 * @param render_by_client 是否由客户端渲染
 * @param after_rendered 渲染完成后的回调,允许修改内容
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type add_global_mail_with_template(
    ::rpc::context &ctx, int32_t mail_template_id, const PROJECT_NAMESPACE_ID::DMailUserInfo *sender, uint32_t zone_id,
    bool render_by_client,
    atfw::util::nostd::function_ref<void(PROJECT_NAMESPACE_ID::DMailContent &mail_content)> after_rendered);

/**
 * @param zone_id 必须正确填大区ID，0则是跨区服全服邮件
 * @param mail_id 邮件ID
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API result_code_type remove_global_mail(::rpc::context &ctx, uint32_t zone_id,
                                                                               int64_t mail_id);
}  // namespace mail
}  // namespace rpc
