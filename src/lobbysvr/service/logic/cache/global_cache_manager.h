// Copyright 2026 atframework

#pragma once

#include <config/compiler_features.h>

#include <design_pattern/singleton.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.cache.common.pb.h>
#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.cache.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_atfw_memory_lru_map.h>

#include <data/player_key_hash_helper.h>
#include <dispatcher/task_manager.h>

#include <mem_pool/lru_map.h>
#include <rpc/cache/cache_algorithm.h>

#include <cstdint>
#include <ctime>

struct hot_data {
 public:
  inline hot_data() noexcept : cache_timeout_(0) {}

  bool is_cache_cold() const noexcept;

  void fetch_data(PROJECT_NAMESPACE_ID::object_cache_content& out) const;

  void set_expired() noexcept;

  void update_data(const PROJECT_NAMESPACE_ID::object_cache_key& input_cache_key,
                   const PROJECT_NAMESPACE_ID::object_cache_content& input_cache_content);

  void refresh_cache() noexcept;

  int64_t data_version() const noexcept;

  inline time_t cache_timeout() const noexcept { return cache_timeout_; }
  inline const PROJECT_NAMESPACE_ID::object_cache_key& cache_key() const noexcept { return cache_key_; }

 private:
  time_t cache_timeout_;
  PROJECT_NAMESPACE_ID::object_cache_key cache_key_;
  PROJECT_NAMESPACE_ID::object_cache_content cache_content_;
};

class player;

class global_cache_manager : public atfw::util::design_pattern::local_singleton<global_cache_manager> {
 public:
  void tick();
  int init();

  std::string memory_leak_debug();

 public:
  // cachesvr通知缓存脏
  void hot_data_expired(const PROJECT_NAMESPACE_ID::object_cache_key& expired_key);

  // 获取数据
  void fetch_hot_data(
      std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                         rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>& cache_keys,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content>& output);

  void fetch_hot_data(
      std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                         rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>& cache_keys,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DUserBasicData>& output);

  void fetch_hot_data(
      std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, PROJECT_NAMESPACE_ID::EnCacheApiGetCacheType,
                         rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>& cache_keys,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DCacheApiObjectData>& output);

  // 更新本地数据
  void update_hot_data(const PROJECT_NAMESPACE_ID::object_cache_key& cache_key,
                       const PROJECT_NAMESPACE_ID::object_cache_content& basic_data);

  void hot_data_debug();

 private:
  // 缓存续期
  void hot_data_watch();
  // 删除过期缓存
  void remove_cold_data();

  atfw::memory::util::lru_map_st<PROJECT_NAMESPACE_ID::object_cache_key, hot_data, rpc::cache_api::cache_key_hash_t,
                                 rpc::cache_api::cache_key_equal_t>
      hot_data_map_;
  time_t watch_heartbeat_timepoint_;
  time_t last_time_tick_;
};
