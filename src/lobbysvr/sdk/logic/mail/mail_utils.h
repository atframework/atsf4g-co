// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#pragma once

#include <std/explicit_declare.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.mail.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

// #include <config/excel/config_set_ResMailTemplate.h>

#include <config/server_frame_build_feature.h>

#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <string>
#include <unordered_map>

namespace excel {
struct config_group_t;
}

namespace rpc {
class context;
}

namespace atframework {
namespace mail {
using mail_render_extension_type = std::unordered_map<std::string, gsl::string_view>;

LOBBY_RPC_API int mail_setup_template_engine(::excel::config_group_t& group);

/**
 * @brief 渲染邮件模板
 * @param mail_template_id 模板ID
 * @param out 数据数据
 * @param receiver 收件人信息
 * @param sender
 * 发件人信息，如果留空且邮件模板配置了使用系统管理员发送，则发件人时系统管理员，user_id为0;如果留空且邮件模板未配置成使用系统管理员发送，则使用收件人未发件人
 * @param extensions 客户端渲染的格外信息
 * @param render_by_client 模板是否由客户端渲染
 * @param item_offest 附件道具（offest结构，如果填写了此字段将不会再填充模板中的附件
 * @param item_detail 附件道具（Ditem结构）包含道具所有详细信息，如果填写了此字段将不会再填充模板中的附件
 * @return 0或错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API int32_t mail_render_template(
    rpc::context& ctx, int32_t mail_template_id, PROJECT_NAMESPACE_ID::DMailContent& out,
    const PROJECT_NAMESPACE_ID::DMailUserInfo& receiver, const PROJECT_NAMESPACE_ID::DMailUserInfo* sender,
    const std::unordered_map<std::string, gsl::string_view>* extensions = nullptr, bool render_by_client = false,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemOffset>* item_offest = nullptr,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>* item_detail = nullptr
    // , google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DConditionRule>* extra_mail_limit = nullptr
);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API int32_t mail_is_valid(rpc::context& ctx,
                                                                 const PROJECT_NAMESPACE_ID::DMailRecord& record);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API int32_t mail_is_valid(rpc::context& ctx,
                                                                 const PROJECT_NAMESPACE_ID::DMailContent& content,
                                                                 std::chrono::system_clock::time_point expired_time);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API bool mail_is_expired_or_removed(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API bool mail_is_expired_or_removed(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API bool mail_is_removed(rpc::context& ctx,
                                                                const PROJECT_NAMESPACE_ID::DMailRecord& content);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API bool mail_is_removed(rpc::context& ctx,
                                                                const PROJECT_NAMESPACE_ID::DMailContent& content);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API bool mail_is_history_removable(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record);

ATFW_EXPLICIT_NODISCARD_ATTR LOBBY_RPC_API bool mail_is_history_removable(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content);

LOBBY_RPC_API void mail_merge_content_and_record(rpc::context& ctx, PROJECT_NAMESPACE_ID::DMailContent& out,
                                                 const PROJECT_NAMESPACE_ID::DMailContent& content,
                                                 const PROJECT_NAMESPACE_ID::DMailRecord& record);

LOBBY_RPC_API void mail_fill_admin_sender(PROJECT_NAMESPACE_ID::DMailUserInfo& sender);

/**
 * @brief 对齐过期时间到下一个天的开始时间（可以设置偏移），很多情况下邮件的过期时间要对齐到天
 */
LOBBY_RPC_API std::chrono::system_clock::time_point mail_padding_day_time(
    std::chrono::system_clock::time_point in,
    std::chrono::system_clock::duration off = std::chrono::system_clock::duration::zero());

}  // namespace mail
}  // namespace atframework
