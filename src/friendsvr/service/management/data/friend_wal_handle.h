// Copyright 2026 atframework
// Created by owent on 2026-09-22.
//

#pragma once

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.friend_api.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <distributed_system/wal_publisher.h>

#include <data/user_key_hash_helper.h>

#include <cstdint>
#include <functional>

namespace rpc {
class context;
}

// PROJECT_NAMESPACE_BEGIN
// class table_friend_blob_data;
// PROJECT_NAMESPACE_END

namespace atframework {
namespace friend_api {

class table_friend_blob_data;

class friend_object;

class friend_partipator_storege_type {
 public:
  inline explicit friend_partipator_storege_type(const table_friend_blob_data& detail)
      : const_data(&detail), is_const(true) {}
  inline explicit friend_partipator_storege_type(table_friend_blob_data& detail)
      : mutable_data(&detail), is_const(false) {}

  inline const table_friend_blob_data* get_const() const noexcept {
    if (!is_const) {
      return mutable_data;
    }

    return const_data;
  }

  inline table_friend_blob_data* get_mutable() noexcept {
    if (is_const) {
      return nullptr;
    }

    return mutable_data;
  }

 private:
  union {
    table_friend_blob_data* mutable_data;
    const table_friend_blob_data* const_data;
  };
  bool is_const;
};

struct friend_wal_publisher_context {
  std::reference_wrapper<rpc::context> context;
  std::reference_wrapper<int32_t> result_code;

  explicit friend_wal_publisher_context(rpc::context& ctx, int32_t& output_result);
};

struct friend_wal_publisher_log_action_getter {
  DFriendEvent::EventCase operator()(const DFriendEvent&) const noexcept;
};

struct friend_wal_subscriber_private_data {};

struct friend_log_action_hash_type {
  inline size_t operator()(const DFriendEvent::EventCase& key) const noexcept { return std::hash<int>()(key); }
};

struct friend_log_action_equal_type {
  inline bool operator()(const DFriendEvent::EventCase& l, const DFriendEvent::EventCase& r) const noexcept {
    return l == r;
  }
};

struct friend_wal_publisher_log_operator
    : public atfw::util::distributed_system::wal_log_operator<
          int64_t, DFriendEvent, friend_wal_publisher_log_action_getter, std::less<>, friend_log_action_hash_type,
          friend_log_action_equal_type, atfw::memory::stl::allocator<DFriendEvent>,
          atfw::util::distributed_system::wal_mt_mode::kSingleThread> {};

struct friend_wal_subscriber_type
    : public atfw::util::distributed_system::wal_subscriber<
          friend_wal_subscriber_private_data, PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> {};

using friend_wal_publisher_type =
    atfw::util::distributed_system::wal_publisher<friend_partipator_storege_type, friend_wal_publisher_log_operator,
                                                  friend_wal_publisher_context, friend_object*,
                                                  friend_wal_subscriber_type>;

atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type> create_friend_publisher(rpc::context& ctx, friend_object&);

}  // namespace friend_api
}  // namespace atframework
