// Copyright 2026 atframework
// Created by owent at 2026-10-22

#include "logic/algorithm/auth_validate.h"

#include <common/string_oprs.h>
#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/authsvr_config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#ifdef ATFW_MACRO_CHANNEL_WITH_STEAMWORKS_SDK
#  include <steam/steamencryptedappticket.h>
#endif

#include <rpc/rpc_context.h>

#include <cstdint>
#include <unordered_map>
#include <string>

namespace {
static std::unordered_map<uint32_t, validate_fn_t> build_validate_fn_map() {
  std::unordered_map<uint32_t, validate_fn_t> ret;
  ret.emplace(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_ATI_ACCOUNT_INTERNAL), &auth_validate_internal);
#ifdef ATFW_MACRO_CHANNEL_WITH_STEAMWORKS_SDK
  ret.emplace(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_ATI_ACCOUNT_STEAM), &auth_validate_steam);
#endif
  return ret;
}
static const std::unordered_map<uint32_t, validate_fn_t>& get_validate_fn_map() {
  static const std::unordered_map<uint32_t, validate_fn_t> ret = build_validate_fn_map();
  return ret;
}
}  // namespace

validate_fn_t get_validate_fn(uint32_t account_type) {
  auto iter = get_validate_fn_map().find(account_type);
  if (iter != get_validate_fn_map().end()) {
    return iter->second;
  }
  return nullptr;
}

rpc::result_code_type auth_validate_internal(rpc::context& ctx, atfw::util::nostd::string_view openid, bool is_new_user,
                                             PROJECT_NAMESPACE_ID::table_login_auth& db_auth_table,
                                             const PROJECT_NAMESPACE_ID::DAccountData& input_account_data) {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::authsvr_cfg>();
  if (!server_cfg.internal_auth().enable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_INVALID_PLATFORM);
  }

  if (!is_new_user) {
    if (db_auth_table.last_account_data().access() == input_account_data.access()) {
      RPC_RETURN_CODE(0);
    }

    FCTXLOGINFO(ctx, "user {} try to validate auth token failed, invalid token", openid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
  }

  if (is_new_user && !server_cfg.internal_auth().allow_register()) {
    FCTXLOGINFO(ctx, "user {} try to validate auth token failed, not allowed to create new user", openid);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_CREATE_USER_FAILED);
  }

  RPC_RETURN_CODE(0);
}

#ifdef ATFW_MACRO_CHANNEL_WITH_STEAMWORKS_SDK
rpc::result_code_type auth_validate_steam(rpc::context& ctx, atfw::util::nostd::string_view openid,
                                          bool /*is_new_user*/, PROJECT_NAMESPACE_ID::table_login_auth& /*db_auth_table*/,
                                          const PROJECT_NAMESPACE_ID::DAccountData& input_account_data) {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::authsvr_cfg>();
  const auto& steam_auth_cfg = server_cfg.steam_auth();
  if (!steam_auth_cfg.enable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_INVALID_PLATFORM);
  }

  do {
    if (!steam_auth_cfg.enable_offline()) {
      break;
    }

    uint8 decrypted[1024];
    uint32 cbDecrypted = sizeof(decrypted);

    std::string rgub_key = steam_auth_cfg.rgub_key();
    if (rgub_key.empty() && !steam_auth_cfg.rgub_key_hex().empty()) {
      rgub_key.reserve(steam_auth_cfg.rgub_key_hex().size() / 2);
      for (size_t i = 0; i + 1 < steam_auth_cfg.rgub_key_hex().size(); i += 2) {
        char ch1 = atfw::util::string::tolower(steam_auth_cfg.rgub_key_hex()[i]);
        char ch2 = atfw::util::string::tolower(steam_auth_cfg.rgub_key_hex()[i + 1]);
        unsigned char byte = 0;
        if (ch1 >= '0' && ch1 <= '9') {
          byte |= static_cast<unsigned char>(ch1 - '0') << 4;
        } else if (ch1 >= 'a' && ch1 <= 'f') {
          byte |= static_cast<unsigned char>(ch1 - 'a' + 10) << 4;
        }
        if (ch2 >= '0' && ch2 <= '9') {
          byte |= static_cast<unsigned char>(ch2 - '0');
        } else if (ch2 >= 'a' && ch2 <= 'f') {
          byte |= static_cast<unsigned char>(ch2 - 'a' + 10);
        }

        rgub_key.push_back(static_cast<char>(byte));
      }
    }
    if (rgub_key.empty()) {
      FCTXLOGINFO(ctx, "user {} try to validate auth token, but steam auth key is empty", openid);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_INVALID_PLATFORM);
    }

    // 1. 用后台生成的对称密钥解密
    if (!SteamEncryptedAppTicket_BDecryptTicket(reinterpret_cast<const uint8*>(input_account_data.access().data()),
                                                static_cast<uint32>(input_account_data.access().size()), decrypted,
                                                &cbDecrypted, reinterpret_cast<const uint8*>(rgub_key.data()),
                                                static_cast<int>(rgub_key.size()))) {
      FCTXLOGINFO(ctx, "user {} try to validate auth token, decrypt ticket failed", openid);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_OPENAPI_PARSE_FAIL);
    }

    // 2. 确认票据属于本游戏
    if (!SteamEncryptedAppTicket_BIsTicketForApp(decrypted, cbDecrypted, steam_auth_cfg.app_id())) {
      FCTXLOGINFO(ctx, "user {} try to validate auth token, ticket is not for this app", openid);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
    }

    // 3. 检查签发时间（票据签发 21 天后过期）
    uint32 issued = SteamEncryptedAppTicket_GetTicketIssueTime(decrypted, cbDecrypted);
    if (atfw::util::time::time_utility::get_sys_now() - static_cast<time_t>(issued) >
        steam_auth_cfg.offline_token_timeout().seconds()) {
      FCTXLOGINFO(ctx, "user {} try to validate auth token, ticket is not for this app, ticket issue time: {}, now: {}",
                  openid, issued, atfw::util::time::time_utility::get_sys_now());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
    }

    // 4. 取出玩家 SteamID，签发自己的会话凭证
    CSteamID steamID;
    SteamEncryptedAppTicket_GetTicketSteamID(decrypted, cbDecrypted, &steamID);
    if (openid != atfw::util::string::format("{}", steamID.ConvertToUint64())) {
      FCTXLOGINFO(ctx, "user {} try to validate auth token, steamID {} mismatch", openid, steamID.ConvertToUint64());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
    }

    RPC_RETURN_CODE(0);
  } while (false);

  // TODO(any): online validate

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
}
#endif
