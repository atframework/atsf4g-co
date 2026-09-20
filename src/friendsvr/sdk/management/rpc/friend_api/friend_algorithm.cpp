// Copyright 2026 atframework
// Created by owent on 2026-09-20

#include "rpc/friend_api/friend_algorithm.h"

#include <common/string_oprs.h>
#include <string/string_format.h>

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/friend_management_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>
#include <rpc/transaction/transaction_api.h>

#include <memory>
#include <string>
#include <utility>

#include "router/router_friend_manager.h"  // IWYU pragma: keep
#include "rpc/friend_api/friendmanagementservice.atfw.gen.h"

namespace rpc {
namespace friend_api {

using atfw::distributed_system::transaction_client_handle;

FRIEND_SDK_MANAGEMENT_API std::string friend_key_to_transaction_participator_key(uint32_t zone_id, uint64_t user_id) {
  return atfw::util::string::format("friend:{}:{}", zone_id, user_id);
}

FRIEND_SDK_MANAGEMENT_API std::pair<uint32_t, uint64_t> transaction_participator_key_to_friend_key(
    gsl::string_view key) {
  if (key.size() <= 7) {
    return std::pair<uint32_t, uint64_t>{0, 0};
  }
  const char* start = key.data();
  const char* next = start + 7;
  uint32_t zone_id = 0;
  uint64_t user_id = 0;
  next = atfw::util::string::str2int(zone_id, next, key.size() - static_cast<size_t>(next - start));
  if (next != nullptr && next + 1 < start + key.size()) {
    atfw::util::string::str2int(user_id, next + 1, key.size() - static_cast<size_t>(next - start) - 1);
    return std::pair<uint32_t, uint64_t>{zone_id, user_id};
  }

  return std::pair<uint32_t, uint64_t>{0, 0};
}

FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::vtable_type& get_default_transaction_delegator() {
  static std::shared_ptr<transaction_client_handle::vtable_type> ret;
  if (ret) {
    return *ret;
  }

  ret = std::make_shared<transaction_client_handle::vtable_type>();

  ret->prepare_participator =
      [](rpc::context& ctx, transaction_client_handle&, const transaction_client_handle::storage_type& storage,
         const transaction_client_handle::participator_type& participator,
         transaction_client_handle::transaction_participator_failure_reason& failure_reason) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::friend_api::SSFriendTransactionPrepareReq> req_body{ctx};
    rpc::context::message_holder<atfw::friend_api::SSFriendTransactionPrepareRsp> rsp_body{ctx};

    auto* prepare_req = req_body->mutable_transaction_request();
    if (nullptr == prepare_req) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    rpc::transaction_api::pack_participator_request(*prepare_req, storage.data, participator);

    auto router_key = rpc::friend_api::transaction_participator_key_to_friend_key(participator.participator_key());

    auto result = RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_transaction_prepare(
        ctx, atfw::friend_api::router_friend_manager::me()->get_type_id(), router_key.first, router_key.second,
        *req_body, *rsp_body));
    if (rsp_body->has_transaction_response() && rsp_body->transaction_response().has_reason()) {
      protobuf_copy_message(failure_reason, rsp_body->transaction_response().reason());
    }

    if (result < 0 && result != PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED) {
      FCTXLOGERROR(ctx, "rpc::friend_api::transaction_prepare {}, participator={} failed, result : {}({})",
                   storage.data.metadata().transaction_uuid(), participator.participator_key(), result,
                   protobuf_mini_dumper_get_error_msg(result));
    }

    if (result < 0) {
      RPC_RETURN_CODE(result);
    }

    RPC_RETURN_CODE(rsp_body->client_result());
  };

  ret->commit_participator =
      [](rpc::context& ctx, transaction_client_handle& /*handle*/,
         const transaction_client_handle::storage_type& storage,
         const transaction_client_handle::participator_type& participator) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::friend_api::SSFriendTransactionCommitReq> req_body{ctx};
    rpc::context::message_holder<atfw::friend_api::SSFriendTransactionCommitRsp> rsp_body{ctx};

    auto* commit_req = req_body->mutable_transaction_request();
    if (nullptr == commit_req) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    rpc::transaction_api::pack_participator_request(*commit_req, storage.data, participator);

    auto router_key = rpc::friend_api::transaction_participator_key_to_friend_key(participator.participator_key());

    // Just notify, there is no need to wait for the result
    auto result = RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_transaction_commit(
        ctx, atfw::friend_api::router_friend_manager::me()->get_type_id(), router_key.first, router_key.second,
        *req_body, *rsp_body, true));

    if (result < 0) {
      FCTXLOGERROR(ctx, "rpc::friend_api::transaction_commit {}, participator={} failed, result : {}({})",
                   storage.data.metadata().transaction_uuid(), participator.participator_key(), result,
                   protobuf_mini_dumper_get_error_msg(result));
    }

    RPC_RETURN_CODE(result);
  };

  ret->reject_participator =
      [](rpc::context& ctx, transaction_client_handle& /*handle*/,
         const transaction_client_handle::storage_type& storage,
         const transaction_client_handle::participator_type& participator) -> rpc::result_code_type {
    rpc::context::message_holder<atfw::friend_api::SSFriendTransactionRejectReq> req_body{ctx};
    rpc::context::message_holder<atfw::friend_api::SSFriendTransactionRejectRsp> rsp_body{ctx};

    auto* reject_req = req_body->mutable_transaction_request();
    if (nullptr == reject_req) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    rpc::transaction_api::pack_participator_request(*reject_req, storage.data, participator);

    auto router_key = rpc::friend_api::transaction_participator_key_to_friend_key(participator.participator_key());

    // Just notify, there is no need to wait for the result
    auto result = RPC_AWAIT_CODE_RESULT(rpc::friend_api::management_transaction_reject(
        ctx, atfw::friend_api::router_friend_manager::me()->get_type_id(), router_key.first, router_key.second,
        *req_body, *rsp_body, true));

    if (result < 0) {
      FCTXLOGERROR(ctx, "rpc::friend_api::transaction_reject {}, participator={} failed, result : {}({})",
                   storage.data.metadata().transaction_uuid(), participator.participator_key(), result,
                   protobuf_mini_dumper_get_error_msg(result));
    }

    RPC_RETURN_CODE(result);
  };

  return *ret;
}

FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::transaction_options get_normal_transaction_options() {
  static std::unique_ptr<transaction_client_handle::transaction_options> options;
  if (options) {
    return *options;
  }

  options = gsl::make_unique<transaction_client_handle::transaction_options>();

  // 采用数据库CAS一致性算法
  options->replication_read_count = 0;
  options->replication_total_count = 0;
  options->memory_only = false;
  options->timeout = protobuf_to_system_clock(logic_config::me()->get_logic_cfg().transaction().timeout());

  return *options;
}

FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::transaction_options get_force_commit_transaction_options() {
  static std::unique_ptr<transaction_client_handle::transaction_options> options;
  if (options) {
    return *options;
  }

  options = gsl::make_unique<transaction_client_handle::transaction_options>(get_normal_transaction_options());
  options->force_commit = true;
  return *options;
}

}  // namespace friend_api
}  // namespace rpc
