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

struct ATFW_UTIL_SYMBOL_VISIBLE friend_cache::ctor_guard_t {
  uint32_t zone_id = 0;
  uint64_t user_id = 0;
};

struct friend_cache::friend_internal_data_t {
  PROJECT_NAMESPACE_ID::DUserIDKey user_key;
  PROJECT_NAMESPACE_ID::table_friend db_data;
  uint64_t db_version = 0;
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

FRIEND_SDK_MANAGEMENT_API void friend_cache::load(rpc::context& ctx,
                                                  const PROJECT_NAMESPACE_ID::table_friend& db_data) {
  protobuf_copy_message(data_->db_data, db_data);
  data_->db_data.set_user_id(get_user_id());
  data_->db_data.set_zone_id(get_zone_id());

  on_loaded(ctx);
}

FRIEND_SDK_MANAGEMENT_API void friend_cache::on_loaded(rpc::context& /*ctx*/) {}

FRIEND_SDK_MANAGEMENT_API void friend_cache::on_saved(rpc::context& /*ctx*/, uint64_t /*obj_svr_id*/) {}

FRIEND_SDK_MANAGEMENT_API int friend_cache::dump(rpc::context& /*ctx*/, PROJECT_NAMESPACE_ID::table_friend& db_data) {
  protobuf_copy_message(db_data, data_->db_data);

  db_data.set_user_id(get_user_id());
  db_data.set_zone_id(get_zone_id());
  return 0;
}

FRIEND_SDK_MANAGEMENT_API const PROJECT_NAMESPACE_ID::DUserIDKey& friend_cache::get_user_key() const noexcept {
  return data_->user_key;
}

FRIEND_SDK_MANAGEMENT_API uint64_t friend_cache::get_user_id() const noexcept { return data_->user_key.user_id(); }

FRIEND_SDK_MANAGEMENT_API uint32_t friend_cache::get_zone_id() const noexcept { return data_->user_key.zone_id(); }

FRIEND_SDK_MANAGEMENT_API const PROJECT_NAMESPACE_ID::table_friend& friend_cache::get_db_data() const noexcept {
  return data_->db_data;
}

FRIEND_SDK_MANAGEMENT_API void friend_cache::load_and_move_db(rpc::context& ctx,
                                                              PROJECT_NAMESPACE_ID::table_friend&& move_db_data,
                                                              uint64_t version) {
  protobuf_move_message(data_->db_data, std::move(move_db_data));
  data_->db_version = version;

  data_->db_data.set_user_id(get_user_id());
  data_->db_data.set_zone_id(get_zone_id());

  on_loaded(ctx);
}

FRIEND_SDK_MANAGEMENT_API uint64_t friend_cache::get_db_version() const noexcept { return data_->db_version; }

FRIEND_SDK_MANAGEMENT_API void friend_cache::set_db_version(uint64_t v) noexcept { data_->db_version = v; }

FRIEND_SDK_MANAGEMENT_API DFriendStatistics& friend_cache::mutable_statistics() {
  return *data_->db_data.mutable_blob_data()->mutable_statistics();
}

FRIEND_SDK_MANAGEMENT_API const DFriendStatistics& friend_cache::get_statistics() const noexcept {
  return data_->db_data.blob_data().statistics();
}

FRIEND_SDK_MANAGEMENT_API PROJECT_NAMESPACE_ID::table_friend& friend_cache::mutable_db_data() noexcept {
  return data_->db_data;
}

}  // namespace friend_api
}  // namespace atframework
