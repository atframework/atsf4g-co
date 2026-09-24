// Copyright 2026 atframework
// Created by owent on 2026-09-22.
//

#include "data/friend_transaction_participator_handle.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/friend_api/friend_algorithm.h>
#include <rpc/rpc_context.h>

#include <router/router_friend_cache.h>
#include <router/router_friend_manager.h>

#include <utility>

#include "data/friend_object.h"
#include "data/friend_wal_handle.h"

namespace atframework {
namespace friend_api {

namespace {
static size_t _hash_combine(size_t l, size_t r) noexcept { return r + 0x9e3779b9 + (l << 6U) + (l >> 2U); }

static atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle::vtable_type>
create_friend_transaction_vtable() {
  static atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle::vtable_type> ret;
  if (ret) {
    return ret;
  }

  ret = atfw::util::memory::make_strong_rc<friend_transaction_participator_handle::vtable_type>();
  if (!ret) {
    return ret;
  }

  ret->do_event = [](rpc::context& ctx, atframework::distributed_system::transaction_participator_handle& handle,
                     const atframework::distributed_system::transaction_participator_handle::storage_type& storage)
      -> rpc::result_code_type {
    friend_object* friend_obj = reinterpret_cast<friend_object*>(handle.get_private_data());
    if (nullptr == friend_obj) {
      FWLOGERROR("transaction_participator_handle {} should not has no private data", handle.get_participator_key());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    friend_key_type key = static_cast<friend_key_type>(
        rpc::friend_api::transaction_participator_key_to_friend_key(handle.get_participator_key()));
    auto event_data = friend_obj->mutable_transaction_participator_data(ctx, storage.metadata().transaction_uuid(), key,
                                                                        storage.participator_data());
    if (!event_data) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
    }

    int32_t result = 0;
    friend_wal_publisher_context param{ctx, result};

    for (auto& event_log : *event_data->mutable_event_data()) {
      auto wal_log = friend_obj->get_wal_publisher().allocate_log(ctx.logical_now(), DFriendEvent::EVENT_NOT_SET, param,
                                                                  event_log);
      if (!wal_log) {
        FWLOGERROR("malloc DFriendEvent failed");
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
      }
      event_log.set_event_id(wal_log->event_id());
      auto applied = friend_obj->get_wal_publisher().emplace_back_log(std::move(wal_log), param);
      if (applied < atfw::util::distributed_system::wal_result_code::kOk) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
      }
    }

    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  };

  ret->check_prepare = [](rpc::context& ctx, atframework::distributed_system::transaction_participator_handle& handle,
                          atframework::distributed_system::transaction_participator_handle::storage_type& storage,
                          atframework::distributed_system::transaction_participator_failure_reason& /*failure_reason*/)
      -> rpc::result_code_type {
    friend_object* friend_obj = reinterpret_cast<friend_object*>(handle.get_private_data());
    if (nullptr == friend_obj) {
      FWLOGERROR("transaction_participator_handle {} should not has no private data", handle.get_participator_key());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    const auto& transaction_uuid = storage.metadata().transaction_uuid();
    // The transaction SDK owns repeat-prepare handling. A force commit must not run beside an existing transaction.
    if (handle.get_running_transactions().count(transaction_uuid) != 0 ||
        handle.get_finished_transactions().count(transaction_uuid) != 0) {
      RPC_RETURN_CODE(storage.configure().force_commit() ? PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN : 0);
    }

    friend_key_type key = static_cast<friend_key_type>(
        rpc::friend_api::transaction_participator_key_to_friend_key(handle.get_participator_key()));
    // Validate the request before inserting it into the UUID cache; an existing prepare may still own that entry.
    auto event_data = friend_obj->unpack_transaction_participator_data(storage.participator_data());
    if (!event_data) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
    }

    auto result = friend_obj->check_prepare_transcation(ctx, transaction_uuid, event_data->event_data());
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
    if (!friend_obj->cache_transaction_participator_data(ctx, transaction_uuid, key, std::move(event_data))) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    RPC_RETURN_CODE(0);
  };

  ret->check_writable = [](rpc::context&, atframework::distributed_system::transaction_participator_handle& handle,
                           bool& writable) -> int32_t {
    writable = false;
    friend_object* friend_obj = reinterpret_cast<friend_object*>(handle.get_private_data());
    if (nullptr == friend_obj) {
      FWLOGERROR("transaction_participator_handle {} should not has no private data", handle.get_participator_key());
      return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
    }

    router_object_base::key_t router_key(router_friend_manager::me()->get_type_id(), friend_obj->get_zone_id(),
                                         friend_obj->get_user_id());

    router_friend_manager::ptr_t router_cache = router_friend_manager::me()->get_cache(router_key);

    // 仅缓存或缓存失效
    if (!router_cache || !router_cache->is_writable()) {
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    // 对象过期
    if (!router_cache->is_object_equal(*friend_obj)) {
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    writable = true;
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  };

  ret->on_finish_running =
      [](rpc::context& ctx, atframework::distributed_system::transaction_participator_handle& handle,
         const atframework::distributed_system::transaction_participator_handle::storage_type& storage)
      -> rpc::result_code_type {
    friend_object* friend_obj = reinterpret_cast<friend_object*>(handle.get_private_data());
    if (nullptr == friend_obj) {
      FWLOGERROR("transaction_participator_handle {} should not has no private data", handle.get_participator_key());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    friend_obj->remove_transaction_data(ctx, storage.metadata().transaction_uuid());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  };

  ret->on_resolve_task_finished =
      [](rpc::context& ctx,
         atframework::distributed_system::transaction_participator_handle& handle) -> rpc::result_code_type {
    friend_object* friend_obj = reinterpret_cast<friend_object*>(handle.get_private_data());
    if (nullptr == friend_obj) {
      FWLOGERROR("transaction_participator_handle {} should not has no private data", handle.get_participator_key());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    friend_obj->refresh_feature_limit(ctx);
    RPC_AWAIT_IGNORE_RESULT(friend_obj->send_notification(ctx));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  };

  ret->on_finished = [](rpc::context& ctx, friend_transaction_participator_handle& handle,
                        const friend_transaction_participator_handle::storage_type&) -> rpc::result_code_type {
    friend_object* friend_obj = reinterpret_cast<friend_object*>(handle.get_private_data());
    if (nullptr == friend_obj) {
      FWLOGERROR("transaction_participator_handle {} should not has no private data", handle.get_participator_key());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    friend_obj->refresh_feature_limit(ctx);
    RPC_AWAIT_IGNORE_RESULT(friend_obj->send_notification(ctx));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  };

  return ret;
}
}  // namespace

size_t friend_key_hash_type::operator()(const friend_key_type& in) const noexcept {
  std::hash<uint32_t> lh;
  std::hash<uint64_t> rh;

  return _hash_combine(lh(in.zone_id), rh(in.user_id));
}

atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle> create_transaction_handle(rpc::context&,
                                                                                                    uint32_t zone_id,
                                                                                                    uint64_t user_id) {
  auto ret = atfw::util::memory::make_strong_rc<friend_transaction_participator_handle>(
      create_friend_transaction_vtable(),
      rpc::friend_api::friend_key_to_transaction_participator_key(zone_id, user_id));
  if (!ret) {
    return ret;
  }

  return ret;
}

}  // namespace friend_api
}  // namespace atframework
