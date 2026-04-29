// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#include "rpc/cache/cache_algorithm.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <algorithm/murmur_hash.h>

namespace rpc {
namespace cache_api {
CACHE_RPC_API size_t cache_watcher_hash_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_watcher &key) const {
  uint32_t cache_type_id = static_cast<uint32_t>(key.cache_type());
  uint32_t zone_id = key.zone_id();
  uint64_t instance_id = key.instance_id();
  unsigned char buffer[sizeof(cache_type_id) + sizeof(zone_id) + sizeof(instance_id)] = {0};

  memcpy(buffer, &cache_type_id, sizeof(cache_type_id));
  memcpy(buffer + sizeof(cache_type_id), &zone_id, sizeof(zone_id));
  memcpy(buffer + sizeof(cache_type_id) + sizeof(zone_id), &instance_id, sizeof(instance_id));

  uint64_t out[2];
  // 随便搞个素数作magic number
  util::hash::murmur_hash3_x64_128(buffer, static_cast<int>(sizeof(buffer)), 0x05D6649FU, out);

  return out[0];
}

CACHE_RPC_API bool cache_watcher_equal_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_watcher &left,
                                                     const PROJECT_NAMESPACE_ID::object_cache_watcher &right) const {
  return left.cache_type() == right.cache_type() && left.instance_id() == right.instance_id() &&
         left.zone_id() == right.zone_id();
}

CACHE_RPC_API size_t cache_key_hash_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_key &key) const {
  uint32_t cache_type_id = static_cast<uint32_t>(key.cache_type());
  uint32_t zone_id = key.zone_id();
  uint64_t instance_id = key.instance_id();
  unsigned char buffer[sizeof(cache_type_id) + sizeof(zone_id) + sizeof(instance_id)] = {0};

  memcpy(buffer, &cache_type_id, sizeof(cache_type_id));
  memcpy(buffer + sizeof(cache_type_id), &zone_id, sizeof(zone_id));
  memcpy(buffer + sizeof(cache_type_id) + sizeof(zone_id), &instance_id, sizeof(instance_id));

  uint64_t out[2];
  // 随便搞个素数作magic number
  util::hash::murmur_hash3_x64_128(buffer, static_cast<int>(sizeof(buffer)), 0x05D6649FU, out);

  return out[0];
}

CACHE_RPC_API bool cache_key_equal_t::operator()(const PROJECT_NAMESPACE_ID::object_cache_key &left,
                                                 const PROJECT_NAMESPACE_ID::object_cache_key &right) const {
  return left.cache_type() == right.cache_type() && left.instance_id() == right.instance_id() &&
         left.zone_id() == right.zone_id();
}

CACHE_RPC_API void update_cache_content_from_meta(::rpc::context &, PROJECT_NAMESPACE_ID::DUserBasicData &output,
                                                  const PROJECT_NAMESPACE_ID::DUserCacheMeta &input) {
  if (input.has_login_basic()) {
    auto *login_basic = output.mutable_login_data_cache();
    const auto &input_login_basic = input.login_basic();
    login_basic->set_business_register_time(input_login_basic.business_register_time());
    login_basic->set_business_login_time(input_login_basic.business_login_time());
    login_basic->set_business_logout_time(input_login_basic.business_logout_time());
    login_basic->set_business_unregister_time(input_login_basic.business_unregister_time());
  }

  if (input.has_basic_profile()) {
    auto *output_profile = output.mutable_profile();
    const auto &input_profile = input.basic_profile();
    output_profile->set_nick_name(input_profile.nick_name());
    output_profile->set_logo_url(input_profile.logo_url());
    output_profile->set_gender(input_profile.gender());
    output_profile->set_custom_nick_name(input_profile.custom_nick_name());
  }

  if (input.has_user_information()) {
    const auto &input_user_info = input.user_information();
    output.set_user_level(input_user_info.user_level());
  }
  if (input.has_client_information()) {
    const auto &input_client_info = input.client_information();
    output.set_client_version(input_client_info.client_version());
  }

  output.set_user_data_version(input.user_data_version());
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
CACHE_RPC_API void update_cache_meta_from_origin_data(
    ::rpc::context &, PROJECT_NAMESPACE_ID::DUserCacheMeta &output, uint64_t data_version,
    const PROJECT_NAMESPACE_ID::user_login_data *input_login_data,
    const PROJECT_NAMESPACE_ID::user_data *input_user_data,
    const PROJECT_NAMESPACE_ID::DUserProfile *input_user_profile,
    const PROJECT_NAMESPACE_ID::DClientDeviceInfo *input_client_device_info) {
  if (data_version > 0) {
    output.set_user_data_version(data_version);
  }

  if (input_login_data != nullptr) {
    auto *login_basic = output.mutable_login_basic();
    login_basic->set_business_register_time(input_login_data->business_register_time());
    login_basic->set_business_login_time(input_login_data->business_login_time());
    login_basic->set_business_logout_time(input_login_data->business_logout_time());
    login_basic->set_business_unregister_time(input_login_data->business_unregister_time());
  }

  if (input_user_data != nullptr) {
    auto *user_info = output.mutable_user_information();
    user_info->set_user_level(input_user_data->user_level());
  }

  if (input_user_profile != nullptr) {
    auto *profile = output.mutable_basic_profile();
    profile->set_nick_name(input_user_profile->nick_name());
    profile->set_logo_url(input_user_profile->logo_url());
    profile->set_gender(input_user_profile->gender());
    profile->set_custom_nick_name(input_user_profile->custom_nick_name());
  }

  if (input_client_device_info != nullptr) {
    auto *client_info = output.mutable_client_information();
    client_info->set_client_version(input_client_device_info->client_version());
  }
}

}  // namespace cache_api
}  // namespace rpc
