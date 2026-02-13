// Copyright 2021 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_set>

#include "data/player_key_hash_helper.h"
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_shared_message.h"

namespace rpc {
class context;
}

class player_cache;

class player_manager {
 public:
  using player_ptr_t = std::shared_ptr<player_cache>;

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(player_manager)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(player_manager)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(player_manager)
#endif

 private:
  SERVER_FRAME_API player_manager();
  SERVER_FRAME_API ~player_manager();

 public:
  /**
   * @brief 移除用户
   * @param user user指针
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove(rpc::context &ctx, player_ptr_t user,
                                                                        bool force_kickoff = false);

  /**
   * @brief 移除用户
   * @param user_id user_id
   * @param zone_id zone_id
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove(rpc::context &ctx, uint64_t user_id,
                                                                        uint32_t zone_id, bool force_kickoff = false,
                                                                        player_cache *check_user = nullptr);

  /**
   * @brief 启动异步任务移除用户
   * @param user_id user_id
   * @param zone_id zone_id
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  SERVER_FRAME_API void async_remove(rpc::context &ctx, player_ptr_t user, bool force_kickoff = false);

  /**
   * @brief 启动异步任务移除用户
   * @param user_id user_id
   * @param zone_id zone_id
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  SERVER_FRAME_API void async_remove(rpc::context &ctx, uint64_t user_id, uint32_t zone_id, bool force_kickoff = false,
                                     player_cache *check_user = nullptr);

  /**
   * @brief 保存用户数据
   * @param user_id user_id
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type save(rpc::context &ctx, uint64_t user_id,
                                                                      uint32_t zone_id,
                                                                      const player_cache *check_user = nullptr);

  /**
   * @brief 添加到计划保存队列
   * @param user_id user_id
   * @param zone_id zone_id
   * @param kickoff kickoff true表示要下线，路由系统降执行降级操作
   */
  SERVER_FRAME_API bool add_save_schedule(uint64_t user_id, uint32_t zone_id, bool kickoff = false);

  /**
   * @brief 加载指定玩家数据。
   * @note 注意这个函数只是读数据库做缓存。
   * @note lobbysvr 请不要强制拉去数据 会冲掉玩家数据
   * @note 返回的 user 指针不能用于改写玩家数据，不做保存。
   * @param user_id
   * @return null 或者 user指针
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type load(rpc::context &ctx, uint64_t user_id,
                                                                      uint32_t zone_id, player_ptr_t &output,
                                                                      bool force = false);

  SERVER_FRAME_API size_t size() const;

  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type create(
      rpc::context &ctx, uint64_t user_id, uint32_t zone_id, const std::string &openid,
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_login> &login_tb, uint64_t &login_ver, player_ptr_t &output);
  template <typename TPLAYER>
  EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE rpc::result_code_type create_as(
      rpc::context &ctx, uint64_t user_id, uint32_t zone_id, const std::string &openid,
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_login> &login_tb, uint64_t &login_ver,
      std::shared_ptr<TPLAYER> &output) {
    player_ptr_t output_base;
    auto ret = RPC_AWAIT_CODE_RESULT(create(ctx, user_id, zone_id, openid, login_tb, login_ver, output_base));
    output = std::static_pointer_cast<TPLAYER>(output_base);
    RPC_RETURN_CODE(ret);
  }

  SERVER_FRAME_API player_ptr_t find(uint64_t user_id, uint32_t zone_id) const;

  template <typename TPLAYER>
  ATFW_UTIL_SYMBOL_VISIBLE const std::shared_ptr<TPLAYER> find_as(uint64_t user_id, uint32_t zone_id) const {
    return std::static_pointer_cast<TPLAYER>(find(user_id, zone_id));
  }

  SERVER_FRAME_API bool has_create_user_lock(uint64_t user_id, uint32_t zone_id) const noexcept;

 private:
  std::unordered_set<PROJECT_NAMESPACE_ID::DPlayerIDKey, player_key_hash_t, player_key_equal_t> create_user_lock_;
};
