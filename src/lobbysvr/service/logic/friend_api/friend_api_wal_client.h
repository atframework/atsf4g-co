// Copyright 2026 atframework
// Created by owent on 2026-09-28

#pragma once

#include <distributed_system/wal_client.h>

#include <config/server_frame_build_feature.h>

#include <data/user_key_hash_helper.h>

#include "logic/friend_api/friend_api_defs.h"

class user_friend_api_manager;

namespace rpc {
class context;
}

PROJECT_NAMESPACE_BEGIN
class user_friend_data;
PROJECT_NAMESPACE_END

using friend_api_storege_type = PROJECT_NAMESPACE_ID::user_friend_data;
using friend_api_client_private_data_type = user_friend_api_manager*;
using friend_api_client_snapshot_type = atfw::friend_api::table_friend_blob_data;

struct friend_api_wal_client_context {
  std::reference_wrapper<rpc::context> context;
  std::reference_wrapper<int32_t> result_code;

  explicit friend_api_wal_client_context(rpc::context& ctx, int32_t& output_result);
};

struct friend_wal_client_log_action_getter {
  inline int32_t operator()(const atfw::friend_api::DFriendEvent& evt) noexcept { return evt.event_case(); }
};

struct friend_log_action_hash_type {
  inline size_t operator()(const int32_t& key) const noexcept { return std::hash<int32_t>()(key); }
};

struct friend_log_action_equal_type {
  inline bool operator()(const int32_t& l, const int32_t& r) const noexcept { return l == r; }
};

struct friend_api_wal_client_log_operator
    : public atfw::util::distributed_system::wal_log_operator<
          int64_t, atfw::friend_api::DFriendEvent, friend_wal_client_log_action_getter, std::less<int64_t>,
          friend_log_action_hash_type, friend_log_action_equal_type,
          atfw::memory::stl::allocator<atfw::friend_api::DFriendEvent>,
          atfw::util::distributed_system::wal_mt_mode::kSingleThread> {};

using user_friend_api_wal_client_type =
    atfw::util::distributed_system::wal_client<friend_api_storege_type, friend_api_wal_client_log_operator,
                                               friend_api_wal_client_context, friend_api_client_private_data_type,
                                               friend_api_client_snapshot_type>;

atfw::util::memory::strong_rc_ptr<user_friend_api_wal_client_type> user_friend_api_create_wal_client(
    user_friend_api_manager&);
