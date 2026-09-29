// Copyright 2026 atframework
// Created by owent on 2026-09-28

#include "logic/friend_api/friend_api_transaction_client_handle.h"

#include <memory/object_allocator.h>

#include <rpc/friend_api/friend_algorithm.h>
#include <rpc/rpc_context.h>

#include <utility/protobuf_mini_dumper.h>

#include "data/user.h"
#include "logic/friend_api/user_friend_api_manager.h"

namespace {
static atfw::util::memory::strong_rc_ptr<friend_api_transaction_client_handle::vtable_type>
create_user_friend_transaction_vtable() {
  static atfw::util::memory::strong_rc_ptr<friend_api_transaction_client_handle::vtable_type> ret;
  if (ret) {
    return ret;
  }

  ret = atfw::memory::stl::make_strong_rc<friend_api_transaction_client_handle::vtable_type>();
  if (!ret) {
    return ret;
  }

  ret->prepare_participator =
      [](rpc::context& ctx, friend_api_transaction_client_handle& handle,
         const friend_api_transaction_client_handle::storage_type& storage,
         const friend_api_transaction_client_handle::participator_type& participator,
         friend_api_transaction_client_handle::transaction_participator_failure_reason& failure_reason)
      -> rpc::result_code_type {
    user_friend_api_manager* friend_manager = reinterpret_cast<user_friend_api_manager*>(handle.get_private_data());
    if (nullptr == friend_manager) {
      FWLOGERROR("friend_api_transaction_client_handle should not has no private data");
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    auto rpc_res = RPC_AWAIT_CODE_RESULT(rpc::friend_api::get_default_transaction_delegator().prepare_participator(
        ctx, handle, storage, participator, failure_reason));

    if (rpc_res < 0 && rpc_res != PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) {
      FCTXLOGERROR(ctx, "{} rpc::friend_api::transaction_prepare failed, result : {}({})", friend_manager->get_owner(),
                   rpc_res, protobuf_mini_dumper_get_error_msg(rpc_res));
    }

    RPC_RETURN_CODE(rpc_res);
  };

  ret->commit_participator =
      [](rpc::context& ctx, friend_api_transaction_client_handle& handle,
         const friend_api_transaction_client_handle::storage_type& storage,
         const friend_api_transaction_client_handle::participator_type& participator) -> rpc::result_code_type {
    user_friend_api_manager* friend_manager = reinterpret_cast<user_friend_api_manager*>(handle.get_private_data());
    if (nullptr == friend_manager) {
      FWLOGERROR("friend_api_transaction_client_handle should not has no private data");
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    auto rpc_res = RPC_AWAIT_CODE_RESULT(
        rpc::friend_api::get_default_transaction_delegator().commit_participator(ctx, handle, storage, participator));

    if (rpc_res) {
      FCTXLOGERROR(ctx, "{} rpc::friend_api::transaction_commit failed, result : {}({})", friend_manager->get_owner(),
                   rpc_res, protobuf_mini_dumper_get_error_msg(rpc_res));
    }

    RPC_RETURN_CODE(rpc_res);
  };

  ret->reject_participator =
      [](rpc::context& ctx, friend_api_transaction_client_handle& handle,
         const friend_api_transaction_client_handle::storage_type& storage,
         const friend_api_transaction_client_handle::participator_type& participator) -> rpc::result_code_type {
    user_friend_api_manager* friend_manager = reinterpret_cast<user_friend_api_manager*>(handle.get_private_data());
    if (nullptr == friend_manager) {
      FWLOGERROR("friend_api_transaction_client_handle should not has no private data");
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    }

    auto rpc_res = RPC_AWAIT_CODE_RESULT(
        rpc::friend_api::get_default_transaction_delegator().reject_participator(ctx, handle, storage, participator));

    if (rpc_res < 0) {
      FCTXLOGERROR(ctx, "{} rpc::friend_api::transaction_reject failed, result : {}({})", friend_manager->get_owner(),
                   rpc_res, protobuf_mini_dumper_get_error_msg(rpc_res));
    }

    RPC_RETURN_CODE(rpc_res);
  };

  return ret;
}
}  // namespace

atfw::util::memory::strong_rc_ptr<friend_api_transaction_client_handle> user_friend_api_create_transaction_client(
    user_friend_api_manager& friend_mgr) {
  auto ret =
      atfw::memory::stl::make_strong_rc<friend_api_transaction_client_handle>(create_user_friend_transaction_vtable());
  ret->set_private_data(reinterpret_cast<void*>(&friend_mgr));
  return ret;
}
