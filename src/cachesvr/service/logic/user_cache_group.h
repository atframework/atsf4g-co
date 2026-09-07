// Copyright 2026 atframework

#pragma once

#include "logic/cache_group.h"

#include <cstdint>

class cache_group_manager;
class user_cache_group : public cache_group<PROJECT_NAMESPACE_ID::DUserBasicData> {
  using super = cache_group<PROJECT_NAMESPACE_ID::DUserBasicData>;

 public:
  user_cache_group(cache_group_manager &manager);

  int tick(time_t now, int64_t cachesvr_version);

 private:
  ATFW_EXPLICIT_NODISCARD_ATTR static rpc::result_code_type pull_user_cache_fn(::rpc::context &,
                                                                               super::pull_data_param_ptr_t &);
  static void pack_user_cache_fn(rpc::context &, const super::value_type &,
                                 ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &);
  static void update_meta_user_cache_fn(rpc::context &, const PROJECT_NAMESPACE_ID::object_cache_meta &,
                                        super::cache_type &);
};