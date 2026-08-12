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

#include "data/user_key_hash_helper.h"
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_shared_message.h"

namespace rpc {
class context;
}

class user_cache;

class user_manager {
 public:
  using user_ptr_t = std::shared_ptr<user_cache>;

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(user_manager)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(user_manager)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(user_manager)
#endif

 private:
  SERVER_FRAME_API user_manager();
  SERVER_FRAME_API ~user_manager();

 public:
  /**
   * @brief 移除用户
   * @param user_inst user指针
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove(rpc::context &ctx, user_ptr_t user_inst,
                                                                             bool force_kickoff = false);

  /**
   * @brief 移除用户
   * @param user_id user_id
   * @param zone_id zone_id
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove(rpc::context &ctx, uint64_t user_id,
                                                                             uint32_t zone_id,
                                                                             bool force_kickoff = false,
                                                                             user_cache *check_user = nullptr);

  /**
   * @brief 启动异步任务移除用户
   * @param user_id user_id
   * @param zone_id zone_id
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  SERVER_FRAME_API void async_remove(rpc::context &ctx, user_ptr_t user_inst, bool force_kickoff = false);

  /**
   * @brief 启动异步任务移除用户
   * @param user_id user_id
   * @param zone_id zone_id
   * @param force_kickoff 强制移除，不进入离线缓存
   */
  SERVER_FRAME_API void async_remove(rpc::context &ctx, uint64_t user_id, uint32_t zone_id, bool force_kickoff = false,
                                     user_cache *check_user = nullptr);

  /**
   * @brief 保存用户数据
   * @param user_id user_id
   */
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type save(rpc::context &ctx, uint64_t user_id,
                                                                           uint32_t zone_id,
                                                                           const user_cache *check_user = nullptr);

  /**
   * @brief 添加到计划保存队列
   * @param user_id user_id
   * @param zone_id zone_id
   * @param kickoff kickoff true表示要下线，路由系统降执行降级操作
   */
  SERVER_FRAME_API bool add_save_schedule(uint64_t user_id, uint32_t zone_id, bool kickoff = false);

  /**
   * @brief 加载指定用户数据。
   * @note 注意这个函数只是读数据库做缓存。
   * @note lobbysvr 请不要强制拉去数据 会冲掉用户数据
   * @note 返回的 user 指针不能用于改写用户数据，不做保存。
   * @param user_id
   * @return null 或者 user指针
   */
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type load(rpc::context &ctx, uint64_t user_id,
                                                                           uint32_t zone_id, user_ptr_t &output,
                                                                           bool force = false);

  SERVER_FRAME_API size_t size() const;

  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type create(
      rpc::context &ctx, uint64_t user_id, uint32_t zone_id, const std::string &openid,
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> &login_lock_tb, uint64_t login_lock_ver,
      user_ptr_t &output);
  template <typename TUSER>
  ATFW_EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE rpc::result_code_type create_as(
      rpc::context &ctx, uint64_t user_id, uint32_t zone_id, const std::string &openid,
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> &login_lock_tb, uint64_t login_lock_ver,
      std::shared_ptr<TUSER> &output) {
    user_ptr_t output_base;
    auto ret = RPC_AWAIT_CODE_RESULT(create(ctx, user_id, zone_id, openid, login_lock_tb, login_lock_ver, output_base));
    output = std::static_pointer_cast<TUSER>(output_base);
    RPC_RETURN_CODE(ret);
  }

  SERVER_FRAME_API user_ptr_t find(uint64_t user_id, uint32_t zone_id) const;

  template <typename TUSER>
  ATFW_UTIL_SYMBOL_VISIBLE const std::shared_ptr<TUSER> find_as(uint64_t user_id, uint32_t zone_id) const {
    return std::static_pointer_cast<TUSER>(find(user_id, zone_id));
  }

  SERVER_FRAME_API bool has_create_user_lock(uint64_t user_id, uint32_t zone_id) const noexcept;

 private:
  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> create_user_lock_;
};
