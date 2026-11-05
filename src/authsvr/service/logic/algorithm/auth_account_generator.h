// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

#include <cstdint>
#include <string>
#include <vector>

// 预生成的内部鉴权账号，字段与 login_auth 表一致
struct auth_generated_account {
  std::string open_id;
  std::string password;  // login_auth.last_account_data.access 的明文值
  uint64_t user_id;
};

/**
 * @brief 批量预生成内部鉴权账号并写入 login_auth 表
 *
 * 密码写入 login_auth.last_account_data.access，与 auth_validate_internal 的校验方式一致，
 * 因此在关闭注册(internal_auth.allow_register = false)之后，预先写入的账号依然可以登录。
 *
 * @param ctx rpc 上下文，必须在任务内调用
 * @param account_type 账号类型，需要第三方票据的类型(Steam)不能预生成
 * @param open_id_prefix open_id 前缀，最终 open_id 为前缀加 6 位序号
 * @param start_index 起始序号
 * @param count 生成数量
 * @param out 本次成功创建(或补全)的账号，已存在且已有 user_id 的 open_id 会被跳过
 * @return 0 或错误码
 */
rpc::result_code_type auth_generate_accounts(rpc::context &ctx, uint32_t account_type,
                                             const std::string &open_id_prefix, uint64_t start_index, uint32_t count,
                                             std::vector<auth_generated_account> &out);
