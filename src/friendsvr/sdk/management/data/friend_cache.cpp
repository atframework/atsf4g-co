// Copyright 2026 atframework
// Created by owent on 2026-09-18

#include "data/friend_cache.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_context.h>

#include <memory/object_allocator.h>

#include <utility/protobuf_mini_dumper.h>

#include <utility>

namespace atframework {
namespace friend_api {

struct friend_cache::friend_internal_data_t {
  PROJECT_NAMESPACE_ID::DUserIDKey user_key;
  table_friend_blob_data db_blob_data;
  uint64_t db_version = 0;

  uint64_t router_server_id = 0;
  uint64_t router_version = 0;
  std::chrono::system_clock::time_point router_save_timepoint = std::chrono::system_clock::from_time_t(0);
};

FRIEND_SDK_MANAGEMENT_API friend_cache::friend_cache(ctor_guard_t& guard)
    : data_(atfw::component::memory::stl::make_strong_rc<friend_internal_data_t>()) {
  data_->user_key.set_zone_id(guard.zone_id);
  data_->user_key.set_user_id(guard.user_id);

  FWLOGDEBUG("{} created", (*this));
}

FRIEND_SDK_MANAGEMENT_API friend_cache::~friend_cache() { FWLOGDEBUG("{} destroyed", (*this)); }

FRIEND_SDK_MANAGEMENT_API void friend_cache::init(rpc::context& /*ctx*/) {}

FRIEND_SDK_MANAGEMENT_API friend_cache::ptr_t friend_cache::create(rpc::context& ctx, uint32_t zone_id,
                                                                   uint64_t user_id) {
  ctor_guard_t guard;
  guard.zone_id = zone_id;
  guard.user_id = user_id;
  auto ret = atfw::component::memory::stl::make_shared<friend_cache>(guard);
  if (ret) {
    ret->init(ctx);
  }
  return ret;
}

FRIEND_SDK_MANAGEMENT_API void friend_cache::load(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_friend& db_data,
                                                  uint64_t db_version) {
  protobuf_copy_message(data_->db_blob_data, db_data.blob_data());
  data_->router_server_id = db_data.router_server_id();
  data_->router_version = db_data.router_version();
  data_->router_save_timepoint = protobuf_to_system_clock(db_data.router_save_timepoint());

  data_->db_version = db_version;

  on_loaded(ctx);
}

FRIEND_SDK_MANAGEMENT_API void friend_cache::on_loaded(rpc::context& /*ctx*/) {}

FRIEND_SDK_MANAGEMENT_API void friend_cache::on_saved(rpc::context& /*ctx*/, uint64_t /*obj_svr_id*/) {}

FRIEND_SDK_MANAGEMENT_API int friend_cache::dump(rpc::context& /*ctx*/, PROJECT_NAMESPACE_ID::table_friend& db_data) {
  protobuf_copy_message(*db_data.mutable_blob_data(), data_->db_blob_data);

  db_data.set_user_id(get_user_id());
  db_data.set_zone_id(get_zone_id());
  db_data.set_router_server_id(data_->router_server_id);
  db_data.set_router_version(data_->router_version);
  *db_data.mutable_router_save_timepoint() = protobuf_from_system_clock(data_->router_save_timepoint);
  return 0;
}

FRIEND_SDK_MANAGEMENT_API const PROJECT_NAMESPACE_ID::DUserIDKey& friend_cache::get_user_key() const noexcept {
  return data_->user_key;
}

FRIEND_SDK_MANAGEMENT_API uint64_t friend_cache::get_user_id() const noexcept { return data_->user_key.user_id(); }

FRIEND_SDK_MANAGEMENT_API uint32_t friend_cache::get_zone_id() const noexcept { return data_->user_key.zone_id(); }

FRIEND_SDK_MANAGEMENT_API const table_friend_blob_data& friend_cache::get_db_data() const noexcept {
  return data_->db_blob_data;
}

FRIEND_SDK_MANAGEMENT_API void friend_cache::load_and_move_db(rpc::context& ctx,
                                                              PROJECT_NAMESPACE_ID::table_friend&& move_db_data,
                                                              uint64_t db_version) {
  protobuf_move_message(data_->db_blob_data, std::move(*move_db_data.mutable_blob_data()));
  data_->router_server_id = move_db_data.router_server_id();
  data_->router_version = move_db_data.router_version();
  data_->router_save_timepoint = protobuf_to_system_clock(move_db_data.router_save_timepoint());
  data_->db_version = db_version;

  on_loaded(ctx);
}

FRIEND_SDK_MANAGEMENT_API uint64_t friend_cache::get_db_version() const noexcept { return data_->db_version; }

FRIEND_SDK_MANAGEMENT_API void friend_cache::set_db_version(uint64_t v) noexcept { data_->db_version = v; }

FRIEND_SDK_MANAGEMENT_API uint64_t friend_cache::get_router_server_id() const noexcept {
  return data_->router_server_id;
}

FRIEND_SDK_MANAGEMENT_API uint64_t friend_cache::get_router_server_version() const noexcept {
  return data_->router_version;
}

FRIEND_SDK_MANAGEMENT_API std::chrono::system_clock::time_point friend_cache::get_router_server_save_timepoint()
    const noexcept {
  return data_->router_save_timepoint;
}

FRIEND_SDK_MANAGEMENT_API void friend_cache::set_router_server(
    uint64_t server_id, uint64_t server_version, std::chrono::system_clock::time_point save_timepoint) noexcept {
  data_->router_server_id = server_id;
  data_->router_version = server_version;
  data_->router_save_timepoint = save_timepoint;
}

FRIEND_SDK_MANAGEMENT_API DFriendStatistics& friend_cache::mutable_statistics() {
  return *data_->db_blob_data.mutable_statistics();
}

FRIEND_SDK_MANAGEMENT_API const DFriendStatistics& friend_cache::get_statistics() const noexcept {
  return data_->db_blob_data.statistics();
}

FRIEND_SDK_MANAGEMENT_API table_friend_blob_data& friend_cache::mutable_db_data() noexcept {
  return data_->db_blob_data;
}

}  // namespace friend_api
}  // namespace atframework
