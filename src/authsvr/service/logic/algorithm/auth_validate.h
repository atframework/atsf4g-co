// Copyright 2026 atframework
// Created by owent at 2026-10-22

#pragma once

#include <nostd/string_view.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

PROJECT_NAMESPACE_BEGIN
class table_login_auth;

class DAccountData;

PROJECT_NAMESPACE_END

using validate_fn_t = rpc::result_code_type (*)(rpc::context& ctx, atfw::util::nostd::string_view openid,
                                                bool is_new_user, PROJECT_NAMESPACE_ID::table_login_auth& db_auth_table,
                                                const PROJECT_NAMESPACE_ID::DAccountData& input_account_data);

validate_fn_t get_validate_fn(uint32_t account_type);

rpc::result_code_type auth_validate_internal(rpc::context& ctx, atfw::util::nostd::string_view openid, bool is_new_user,
                                             PROJECT_NAMESPACE_ID::table_login_auth& db_auth_table,
                                             const PROJECT_NAMESPACE_ID::DAccountData& input_account_data);

#ifdef ATFW_MACRO_CHANNEL_WITH_STEAMWORKS_SDK
rpc::result_code_type auth_validate_steam(rpc::context& ctx, atfw::util::nostd::string_view openid, bool is_new_user,
                                          PROJECT_NAMESPACE_ID::table_login_auth& db_auth_table,
                                          const PROJECT_NAMESPACE_ID::DAccountData& input_account_data);
#endif
