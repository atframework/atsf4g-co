// Copyright 2026 atframework

#pragma once

#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <design_pattern/noncopyable.h>
#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.cache.common.pb.h>
#include <protocol/pbdesc/com.protocol.cache.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_stl_unordered_map.h>

#include <rpc/cache/cache_algorithm.h>

#include <cstdint>
#include <string>

namespace rpc {
class context;
}

PROJECT_NAMESPACE_BEGIN
class object_cache_pull_key;
PROJECT_NAMESPACE_END

class user;

class user_cache_manager : public atfw::util::design_pattern::noncopyable {
 public:
  explicit user_cache_manager(user& owner);
  ~user_cache_manager();

  std::string memory_leak_debug();

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t login_init(rpc::context&);
  void on_logout(rpc::context&);
  void on_update_session(rpc::context&);
  void on_saved(rpc::context&);
  void refresh_feature_limit_minute(rpc::context& ctx);

  user& get_owner() { return *owner_; }
  const user& get_owner() const { return *owner_; }

  void refresh_feature_limit_second(rpc::context& ctx);

  void set_user_cache_expired();
  void set_user_meta_expired();
  void set_user_meta_expired_delay_sync();
  void pack_user_meta_data(rpc::context& ctx, PROJECT_NAMESPACE_ID::object_cache_meta& cache_meta);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type check_user_id_valid(rpc::context& ctx, uint32_t zone_id,
                                                                         uint64_t user_id);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_user_cache(
      rpc::context& ctx, uint32_t zone_id, uint64_t user_id,
      PROJECT_NAMESPACE_ID::DUserBasicData* ATFW_UTIL_MACRO_NULLABLE out = nullptr);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_cache_expired_notify_to_client(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::object_cache_key& cache_key);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_update_meta_to_client(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::object_cache_meta& meta);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type pull_cache(
      rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>& user_keys,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>& output,
      bool filter_unused_id = false,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiCacheKey>* ATFW_UTIL_MACRO_NULLABLE
          not_found_keys = nullptr);

  int32_t unwatch_cache_keys(
      rpc::context& ctx, PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type,
      const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectKey>& keys);

  int32_t unwatch_cache_keys(rpc::context& ctx,
                             ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_key>&& cache_keys);

  void update_user_cache_info(::rpc::context& ctx);

  // 有些场景希望立刻发生
  int32_t send_update_user_basic_meta_to_cachesvr(rpc::context& ctx);

 private:
  void fill_self_basic_data(PROJECT_NAMESPACE_ID::DUserBasicData& output);
  void async_unwatch_all(rpc::context& ctx);
  void maybe_async_watch_heartbeat(rpc::context& ctx);
  void watch_heartbeat(rpc::context& ctx);

  void send_cache_expired_notify_to_cachesvr(rpc::context& ctx);
  void async_send_update_user_basic_meta_to_cachesvr(rpc::context& ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type pull_one_cache(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::object_cache_key& key,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>& output);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;

  int64_t cachesvr_discovery_version_;
  bool need_notify_user_cache_expired_;
  bool need_notify_user_meta_expired_;
  bool need_notify_user_match_exired_;
  time_t fallback_notify_expired_timepoint_;
  time_t watch_heartbeat_timepoint_;
  bool is_logout_;
  int64_t next_time_meta_update_time_;
  struct watch_data_t {
    uint64_t data_version;
  };

  using watcher_set_t =
      atfw::memory::stl::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, watch_data_t,
                                       rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>;

  watcher_set_t watch_data_;
};
