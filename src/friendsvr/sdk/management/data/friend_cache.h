// Copyright 2026 atframework
// Created by owent on 2026-09-18

#pragma once

#include <config/compile_optimize.h>
#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <log/log_wrapper.h>

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

namespace rpc {
class context;
}

PROJECT_NAMESPACE_BEGIN
class table_friend;
class DUserIDKey;
PROJECT_NAMESPACE_END

namespace atframework {
namespace friends {

class DFriendStatistics;

class ATFW_UTIL_SYMBOL_VISIBLE friend_cache {
  ATFW_UTIL_DESIGN_PATTERN_NOCOPYABLE(friend_cache)
  ATFW_UTIL_DESIGN_PATTERN_NOMOVABLE(friend_cache)

 public:
  using ptr_t = std::shared_ptr<friend_cache>;

 private:
  struct ATFW_UTIL_SYMBOL_VISIBLE ctor_guard_t;

 public:
  FRIEND_SDK_MANAGEMENT_API explicit friend_cache(ctor_guard_t&);
  FRIEND_SDK_MANAGEMENT_API virtual ~friend_cache();

  // 初始化，默认数据
  FRIEND_SDK_MANAGEMENT_API virtual void init(rpc::context& ctx);

  FRIEND_SDK_MANAGEMENT_API static ptr_t create(rpc::context& ctx, uint32_t zone_id, uint64_t user_id);

  // 从table数据初始化
  FRIEND_SDK_MANAGEMENT_API virtual void load(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_friend& db_data);

  /**
   * @brief 数据库加载完/更新完触发的事件
   */
  FRIEND_SDK_MANAGEMENT_API virtual void on_loaded(rpc::context& ctx);

  /**
   * @brief 数据库保存成功后触发的事件
   */
  FRIEND_SDK_MANAGEMENT_API virtual void on_saved(rpc::context& ctx, uint64_t obj_svr_id);

  /**
   * @brief 转储数据
   * @param user 转储目标
   * @return 0或错误码
   */
  FRIEND_SDK_MANAGEMENT_API virtual int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_friend& db_data);

  FRIEND_SDK_MANAGEMENT_API const PROJECT_NAMESPACE_ID::DUserIDKey& get_user_key() const noexcept;
  FRIEND_SDK_MANAGEMENT_API uint64_t get_user_id() const noexcept;
  FRIEND_SDK_MANAGEMENT_API uint32_t get_zone_id() const noexcept;

  FRIEND_SDK_MANAGEMENT_API const PROJECT_NAMESPACE_ID::table_friend& get_db_data() const noexcept;
  FRIEND_SDK_MANAGEMENT_API void load_and_move_db(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_friend&& move_db_data,
                                                  uint64_t version);

  FRIEND_SDK_MANAGEMENT_API uint64_t get_db_version() const noexcept;
  FRIEND_SDK_MANAGEMENT_API void set_db_version(uint64_t v) noexcept;

  FRIEND_SDK_MANAGEMENT_API DFriendStatistics& mutable_statistics();
  FRIEND_SDK_MANAGEMENT_API const DFriendStatistics& get_statistics() const noexcept;

 protected:
  FRIEND_SDK_MANAGEMENT_API PROJECT_NAMESPACE_ID::table_friend& mutable_db_data() noexcept;

 private:
  friend class router_friend_cache;

 private:
  struct friend_internal_data_t;

  atfw::util::memory::strong_rc_ptr<friend_internal_data_t> data_;
};

}  // namespace friends
}  // namespace atframework

namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID {
template <class CharT>
struct ATFW_UTIL_SYMBOL_VISIBLE
formatter<atfw::friends::friend_cache, CharT> : formatter<basic_string_view<CharT>, CharT> {
  template <class FormatContext>
  ATFW_UTIL_FORCEINLINE auto format(const atfw::friends::friend_cache& friend_object, FormatContext& ctx) const {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "friend {}:{}", friend_object.get_zone_id(),
                                       friend_object.get_user_id());
  }
};
}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID
