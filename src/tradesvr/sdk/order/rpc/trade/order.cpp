// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#include "rpc/trade/order.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.struct.trade.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/trade_order_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_utils.h>

#include <rpc/trade_order/tradeorderservice.atfw.gen.h>

#include <utility/protobuf_mini_dumper.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace atframework {
namespace trade {

namespace {
template <class Request, class Response>
static rpc::result_code_type batch_fetch(rpc::context& ctx, const std::unordered_map<uint64_t, Request*>& requests,
                                         std::unordered_map<uint64_t, Response*>& responses,
                                         rpc::result_code_type (*send)(rpc::context&, uint64_t, Request&, Response&,
                                                                       bool, dispatcher_await_options*),
                                         bool (*unpack)(const std::string&, Response&)) {
  struct pending_response {
    uint64_t server_id;
    uint64_t sequence;
    Response* body;
  };
  std::vector<pending_response> pending;
  std::unordered_set<dispatcher_await_options> waiters;
  pending.reserve(requests.size());
  waiters.reserve(requests.size());
  int32_t result = 0;
  for (const auto& request : requests) {
    if (request.first == 0 || request.second == nullptr) {
      continue;
    }
    bool new_response = responses.end() == responses.find(request.first);
    auto*& response = responses[request.first];
    if (response == nullptr || new_response) {
      response = ctx.create<Response>();
      if (response == nullptr) {
        FCTXLOGERROR(ctx, "Cannot allocate {} for server {:#x}", Response::descriptor()->full_name(), request.first);
        result = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        continue;
      }
    } else {
      response->Clear();
    }
    auto waiter = dispatcher_make_default<dispatcher_await_options>();
    int32_t send_result = RPC_AWAIT_CODE_RESULT(send(ctx, request.first, *request.second, *response, false, &waiter));
    if (send_result != 0) {
      FCTXLOGERROR(ctx, "Batch fetch to server {:#x} failed: {}({})", request.first, send_result,
                   protobuf_mini_dumper_get_error_msg(send_result));
      result = send_result;
      continue;
    }
    pending.push_back({request.first, waiter.sequence, response});
    waiters.emplace(std::move(waiter));
  }
  if (waiters.empty()) {
    RPC_RETURN_CODE(result);
  }

  std::unordered_map<uint64_t, atfw::SSMsg> received;
  int32_t wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, waiters, received));
  if (wait_result != 0) {
    FCTXLOGERROR(ctx, "Waiting for batch fetch responses failed: {}({})", wait_result,
                 protobuf_mini_dumper_get_error_msg(wait_result));
    result = wait_result;
  }
  for (const auto& target : pending) {
    auto iter = received.find(target.sequence);
    if (iter == received.end()) {
      if (wait_result == 0) {
        FCTXLOGERROR(ctx, "Missing batch fetch response from server {:#x} for sequence {}", target.server_id,
                     target.sequence);
        result = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
      }
      continue;
    }
    const auto& message = iter->second;
    if (message.head().error_code() != 0) {
      result = message.head().error_code();
      FCTXLOGERROR(ctx, "Batch fetch on server {:#x} failed: {}({})", target.server_id, result,
                   protobuf_mini_dumper_get_error_msg(result));
      if (message.body_bin().empty()) {
        continue;
      }
    }
    if (message.head().rpc_response().type_url() != Response::descriptor()->full_name() ||
        !unpack(message.body_bin(), *target.body)) {
      FCTXLOGERROR(ctx, "Invalid batch fetch response from server {:#x} for sequence {}", target.server_id,
                   target.sequence);
      target.body->Clear();
      if (message.head().error_code() == 0) {
        result = PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
      }
    }
  }
  RPC_RETURN_CODE(result);
}
}  // namespace

TRADE_SDK_ORDER_API rpc::result_code_type batch_try_fetch_lock(
    rpc::context& ctx, std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchLockOrderReq*>& request_bodys,
    std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchLockOrderRsp*>& response_bodys) {
  return batch_fetch(ctx, request_bodys, response_bodys, rpc::trade_order::fetch_try_lock,
                     rpc::trade_order::packer::unpack_fetch_try_lock);
}

TRADE_SDK_ORDER_API rpc::result_code_type batch_fetch_unlock(
    rpc::context& ctx, std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchUnlockOrderReq*>& request_bodys,
    std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchUnlockOrderRsp*>& response_bodys) {
  return batch_fetch(ctx, request_bodys, response_bodys, rpc::trade_order::fetch_unlock,
                     rpc::trade_order::packer::unpack_fetch_unlock);
}

TRADE_SDK_ORDER_API bool will_product_confirmation_reset_guid(
    const PROJECT_NAMESPACE_ID::config::ExcelTradeCommodity& cfg) {
  return !cfg.force_save_db();
}

}  // namespace trade
}  // namespace atframework
