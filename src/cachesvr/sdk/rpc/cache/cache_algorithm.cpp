// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#include "rpc/cache/cache_algorithm.h"

#include <nostd/function_ref.h>
#include <nostd/nullability.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.cache.common.pb.h>
#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <algorithm/murmur_hash.h>
#include <rpc/rpc_context.h>

#include <type_traits>
#include <utility>

namespace rpc {
namespace cache_api {

namespace {
template <class AnyType>
std::pair<bool, bool> internal_try_unpack(
    ::rpc::context &ctx, const ::google::protobuf::Any &input,
    atfw::util::nostd::function_ref<AnyType * ATFW_UTIL_MACRO_NONNULL()> callback) {
  if (input.Is<AnyType>()) {
    if (!input.UnpackTo(callback())) {
      FCTXLOGERROR(ctx, "unpack {} failed", AnyType::descriptor()->full_name());
      return {true, false};
    }

    return {true, true};
  }

  return {false, false};
}
}  // namespace

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
                                                  const PROJECT_NAMESPACE_ID::DUserBasicDataMeta &input) {
  protobuf_copy_message(*output.mutable_meta_data(), input);
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
CACHE_RPC_API void update_cache_meta_from_origin_data(
    ::rpc::context &, PROJECT_NAMESPACE_ID::DUserBasicDataMeta &output, uint64_t data_version,
    const PROJECT_NAMESPACE_ID::user_login_data *input_login_data, const PROJECT_NAMESPACE_ID::user_data *,
    const PROJECT_NAMESPACE_ID::DUserProfile *input_user_profile, const PROJECT_NAMESPACE_ID::DClientDeviceInfo *) {
  if (data_version > 0) {
    output.set_user_data_version(data_version);
  }

  if (input_login_data != nullptr) {
    auto *login_basic = output.mutable_login_data();
    login_basic->set_business_register_time(input_login_data->business_register_time());
    login_basic->set_business_login_time(input_login_data->business_login_time());
    login_basic->set_business_logout_time(input_login_data->business_logout_time());
    login_basic->set_business_unregister_time(input_login_data->business_unregister_time());
  }

  if (input_user_profile != nullptr) {
    protobuf_copy_message(*output.mutable_profile(), *input_user_profile);
  }
}

CACHE_RPC_API void pick_key_from_meta(::rpc::context & /*ctx*/, PROJECT_NAMESPACE_ID::object_cache_key &output,
                                      const PROJECT_NAMESPACE_ID::DCacheApiMetaData &input) {
  switch (input.meta_data_case()) {
    case PROJECT_NAMESPACE_ID::DCacheApiMetaData::kUserMeta: {
      const auto &user_meta = input.user_meta();
      output.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
      output.set_zone_id(user_meta.user_key().zone_id());
      output.set_instance_id(user_meta.user_key().user_id());
      return;
    }
    default:
      break;
  }

  output.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN);
  output.set_zone_id(0);
  output.set_instance_id(0);
}

CACHE_RPC_API void pick_key_from_content(::rpc::context & /*ctx*/, PROJECT_NAMESPACE_ID::object_cache_key &output,
                                         const PROJECT_NAMESPACE_ID::DCacheApiObjectData &input) {
  switch (input.object_data_case()) {
    case PROJECT_NAMESPACE_ID::DCacheApiObjectData::kUserCache: {
      const auto &user_cache = input.user_cache();
      output.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
      output.set_zone_id(user_cache.meta_data().user_key().zone_id());
      output.set_instance_id(user_cache.meta_data().user_key().user_id());
      return;
    }
    default:
      break;
  }

  output.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN);
  output.set_zone_id(0);
  output.set_instance_id(0);
}

CACHE_RPC_API bool pack_cache_meta_to_any(::rpc::context & /*ctx*/, google::protobuf::Any &output,
                                          const PROJECT_NAMESPACE_ID::DCacheApiMetaData &input) {
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  const auto *desc = input.GetDescriptor();
  if (desc == nullptr) {
    return false;
  }

  for (int i = 0; i < desc->field_count(); ++i) {
    const auto *field = desc->field(i);
    if (field == nullptr) {
      continue;
    }

    if (field->is_repeated() || field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }

    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    if (!input.GetReflection()->HasField(input, field)) {
      continue;
    }

    // 只要有一个字段有值，就认为这个meta是有意义的，可以打包
    if (output.PackFrom(input)) {
      return true;
    }
  }

  return false;
}

CACHE_RPC_API bool unpack_cache_meta_from_any(::rpc::context &ctx, PROJECT_NAMESPACE_ID::DCacheApiMetaData &output,
                                              const google::protobuf::Any &input) {
  if (input.type_url().empty()) {
    return false;
  }

  auto res = internal_try_unpack<PROJECT_NAMESPACE_ID::DUserBasicDataMeta>(
      ctx, input, [&output]() { return output.mutable_user_meta(); });

  if (res.first) {
    return res.second;
  }

  return false;
}

CACHE_RPC_API bool pack_cache_content_to_any(::rpc::context & /*ctx*/, google::protobuf::Any &output,
                                             const PROJECT_NAMESPACE_ID::DCacheApiObjectData &input) {
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  const auto *desc = input.GetDescriptor();
  if (desc == nullptr) {
    return false;
  }

  for (int i = 0; i < desc->field_count(); ++i) {
    const auto *field = desc->field(i);
    if (field == nullptr) {
      continue;
    }

    if (field->is_repeated() || field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }

    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    if (!input.GetReflection()->HasField(input, field)) {
      continue;
    }

    // 只要有一个字段有值，就认为这个meta是有意义的，可以打包
    if (output.PackFrom(input)) {
      return true;
    }
  }

  return false;
}

CACHE_RPC_API bool unpack_cache_content_from_any(::rpc::context &ctx, PROJECT_NAMESPACE_ID::DCacheApiObjectData &output,
                                                 const google::protobuf::Any &input) {
  if (input.type_url().empty()) {
    return false;
  }

  auto res = internal_try_unpack<PROJECT_NAMESPACE_ID::DUserBasicData>(
      ctx, input, [&output]() { return output.mutable_user_cache(); });

  if (res.first) {
    return res.second;
  }

  return false;
}

}  // namespace cache_api
}  // namespace rpc
