// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#pragma once

#include <transaction_participator_handle.h>

#include <config/server_frame_build_feature.h>

#ifdef __cpp_impl_three_way_comparison
#  include <compare>
#endif
#include <utility>

namespace rpc {
class context;
}

namespace atframework {
namespace friend_api {

using friend_transaction_participator_handle = atframework::distributed_system::transaction_participator_handle;
class friend_object;

struct friend_key_type {
  uint32_t zone_id;
  uint64_t user_id;

  inline friend_key_type() noexcept : zone_id(0), user_id(0) {}
  inline explicit friend_key_type(const std::pair<uint32_t, uint64_t>& d) noexcept
      : zone_id(d.first), user_id(d.second) {}
  inline explicit friend_key_type(uint32_t input_zone_id, uint64_t input_user_id) noexcept
      : zone_id(input_zone_id), user_id(input_user_id) {}

  inline bool operator==(const friend_key_type& other) const noexcept {
    return zone_id == other.zone_id && user_id == other.user_id;
  }

#ifdef __cpp_impl_three_way_comparison
  inline std::strong_ordering operator<=>(const friend_key_type& other) const noexcept {
    if (zone_id != other.zone_id) {
      return zone_id <=> other.zone_id;
    }
    return user_id <=> other.user_id;
  }
#else
  inline bool operator!=(const friend_key_type& other) const noexcept {
    return zone_id != other.zone_id || user_id != other.user_id;
  }

  inline bool operator<(const friend_key_type& other) const noexcept {
    if (zone_id != other.zone_id) {
      return zone_id < other.zone_id;
    }
    return user_id < other.user_id;
  }

  inline bool operator<=(const friend_key_type& other) const noexcept {
    if (zone_id != other.zone_id) {
      return zone_id <= other.zone_id;
    }
    return user_id <= other.user_id;
  }

  inline bool operator>(const friend_key_type& other) const noexcept {
    if (zone_id != other.zone_id) {
      return zone_id > other.zone_id;
    }
    return user_id > other.user_id;
  }

  inline bool operator>=(const friend_key_type& other) const noexcept {
    if (zone_id != other.zone_id) {
      return zone_id >= other.zone_id;
    }
    return user_id >= other.user_id;
  }
#endif
};

struct friend_key_hash_type {
  size_t operator()(const friend_key_type&) const noexcept;
};

atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle> create_transaction_handle(rpc::context&,
                                                                                                    friend_object&,
                                                                                                    uint32_t zone_id,
                                                                                                    uint64_t user_id);
}  // namespace friend_api
}  // namespace atframework
