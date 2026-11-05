// Copyright 2026 atframework

#include "logic/algorithm/auth_account_generator.h"

#include <algorithm/base64.h>
#include <log/log_wrapper.h>
#include <random/uuid_generator.h>
#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/user/user_basic.h>

#include <string>
#include <utility>
#include <vector>

namespace {
// open_id 序号部分固定 6 位宽度，便于导出后按序号批量引用
std::string make_open_id(const std::string &prefix, uint64_t index) {
  return atfw::util::string::format("{}{:06}", prefix, index);
}
}  // namespace

rpc::result_code_type auth_generate_accounts(rpc::context &ctx, uint32_t account_type,
                                             const std::string &open_id_prefix, uint64_t start_index, uint32_t count,
                                             std::vector<auth_generated_account> &out) {
  if (account_type == static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_ATI_ACCOUNT_STEAM)) {
    FWLOGERROR("auth generate accounts: account type {} needs third-party ticket, can not be pre-generated",
               account_type);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_INVALID_PLATFORM);
  }

  out.clear();
  if (count == 0) {
    RPC_RETURN_CODE(0);
  }
  out.reserve(count);

  for (uint32_t i = 0; i < count; ++i) {
    std::string open_id = make_open_id(open_id_prefix, start_index + i);

    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> login_auth_tb{ctx};
    uint64_t login_auth_cas_version = 0;
    int res = RPC_AWAIT_CODE_RESULT(
        rpc::db::login_auth::get_all(ctx, account_type, open_id, *login_auth_tb, login_auth_cas_version));
    if (res == 0) {
      // 已经存在的账号不覆盖密码和 user_id，避免误改线上数据
      FWLOGWARNING("auth generate accounts: open_id {} already exists(user_id: {}), skipped", open_id,
                   login_auth_tb->user_id());
      continue;
    }
    if (res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      FWLOGERROR("auth generate accounts: open_id {} read login_auth failed, error code: {}", open_id, res);
      RPC_RETURN_CODE(res);
    }

    int64_t user_id = RPC_AWAIT_CODE_RESULT(rpc::user::allocate_user_id(ctx));
    if (user_id <= 0) {
      FWLOGERROR("auth generate accounts: open_id {} allocate user_id failed, error code: {}", open_id,
                 static_cast<int>(user_id));
      RPC_RETURN_CODE(static_cast<int>(user_id));
    }

    // 内部鉴权直接比对明文 access，密码用系统随机源生成 uuid 后转 base64，避免短 uuid 的序号可预测
    std::string password;
    if (0 != atfw::util::base64_encode(password, atfw::util::random::uuid_generator::generate_string_random())) {
      FWLOGERROR("auth generate accounts: open_id {} encode password failed", open_id);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }

    login_auth_tb->set_account_type(account_type);
    login_auth_tb->set_open_id(open_id);
    login_auth_tb->set_user_id(static_cast<uint64_t>(user_id));

    // 预置的账号数据必须与之后登录时的 account_type 一致，否则会被判定为更换认证渠道
    PROJECT_NAMESPACE_ID::DAccountData *account_data = login_auth_tb->mutable_last_account_data();
    account_data->set_account_type(account_type);
    account_data->set_access(password);
    account_data->set_channel_id(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_PCI_NONE));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::insert(ctx, login_auth_tb));
    if (res == PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS) {
      FWLOGWARNING("auth generate accounts: open_id {} is created by another process, skipped", open_id);
      continue;
    }
    if (res < 0) {
      FWLOGERROR("auth generate accounts: open_id {} write login_auth failed, error code: {}", open_id, res);
      RPC_RETURN_CODE(res);
    }

    FWLOGINFO("auth generate accounts: open_id & password: {} & {}", open_id, password);

    auth_generated_account account;
    account.open_id = std::move(open_id);
    account.password = std::move(password);
    account.user_id = static_cast<uint64_t>(user_id);
    out.push_back(std::move(account));
  }

  RPC_RETURN_CODE(0);
}
