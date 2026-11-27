// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#include "rpc/trade/order.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.trade.common.pb.h>
#include <protocol/config/com.struct.trade.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <trade/sharding.h>

#include <rpc/rpc_utils.h>

#include <rpc/trade_order/tradeorderservice.atfw.gen.h>

#include <utility/protobuf_mini_dumper.h>

#include <string>

namespace atframework {
namespace trade {

class DTradeOrderTicketSearchRule;

TRADE_SDK_ORDER_API rpc::result_code_type batch_try_fetch_lock(
    rpc::context& ctx, std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchLockOrderReq*>& request_bodys,
    std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchLockOrderRsp*>& response_bodys) {
  std::unordered_set<dispatcher_await_options> waiters;
  std::unordered_map<uint64_t, atfw::SSMsg> received;

  waiters.reserve(request_bodys.size());
  received.reserve(request_bodys.size());

  rpc::result_code_type::value_type last_error_res = 0;

  for (auto& request_body : request_bodys) {
    if (request_body.first == 0 || request_body.second == nullptr) {
      continue;
    }

    ::atfw::trade::SSTradeOrderFetchLockOrderRsp* response_body = nullptr;
    {
      auto iter_response = response_bodys.find(request_body.first);
      if (iter_response != response_bodys.end()) {
        response_body = iter_response->second;
      }
      if (nullptr == response_body) {
        response_body = ctx.create<::atfw::trade::SSTradeOrderFetchLockOrderRsp>();
      }
      if (nullptr == response_body) {
        FCTXLOGERROR(ctx, "malloc {} for server instance id {} failed", "SSTradeOrderFetchLockOrderRsp",
                     request_body.first);
        continue;
      }
    }

    // atframework::trade::SSTradeFetchLockOrderReq
    // atframework::trade::SSTradeOrderFetchLockOrderReq
    dispatcher_await_options waiter_options = dispatcher_make_default<dispatcher_await_options>();
    int32_t res = RPC_AWAIT_CODE_RESULT(::rpc::trade_order::fetch_try_lock(
        ctx, request_body.first, *request_body.second, *response_body, false, &waiter_options));
    if (res < 0) {
      FCTXLOGERROR(ctx, "Try to call fetch_try_lock to server {}({:#x}) failed, res: {}({})", request_body.first,
                   request_body.first, res, protobuf_mini_dumper_get_error_msg(res));
      last_error_res = res;
    } else {
      waiters.emplace(std::move(waiter_options));
    }
  }

  if (!waiters.empty()) {
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, waiters, received));
    if (res < 0) {
      FCTXLOGERROR(ctx, "Try to wait multiple response failed, res: {}({})", res,
                   protobuf_mini_dumper_get_error_msg(res));
      last_error_res = res;
    }
  }
  for (auto& received_message : received) {
    if (received_message.second.body_bin().empty()) {
      continue;
    }

    auto iter_response = response_bodys.find(received_message.first);
    if (iter_response == response_bodys.end()) {
      FCTXLOGERROR(ctx, "Got unexpected response from {}({:#x})", received_message.first, received_message.first);
      continue;
    }
    if (iter_response->second == nullptr) {
      FCTXLOGERROR(ctx, "Got unexpected response from {}({:#x})", received_message.first, received_message.first);
      continue;
    }
    if (!::rpc::trade_order::packer::unpack_fetch_try_lock(received_message.second.body_bin(),
                                                           *iter_response->second)) {
      FCTXLOGERROR(ctx, "Unpack response from {}({:#x}) failed: {}", received_message.first, received_message.first,
                   iter_response->second->InitializationErrorString());
    }
  }

  RPC_RETURN_CODE(last_error_res);
}

TRADE_SDK_ORDER_API rpc::result_code_type batch_fetch_unlock(
    rpc::context& ctx, std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchUnlockOrderReq*>& request_bodys,
    std::unordered_map<uint64_t, ::atfw::trade::SSTradeOrderFetchUnlockOrderRsp*>& response_bodys) {
  std::unordered_set<dispatcher_await_options> waiters;
  std::unordered_map<uint64_t, atfw::SSMsg> received;

  waiters.reserve(request_bodys.size());
  received.reserve(request_bodys.size());

  rpc::result_code_type::value_type last_error_res = 0;

  for (auto& request_body : request_bodys) {
    if (request_body.first == 0 || request_body.second == nullptr) {
      continue;
    }

    ::atfw::trade::SSTradeOrderFetchUnlockOrderRsp* response_body = nullptr;
    {
      auto iter_response = response_bodys.find(request_body.first);
      if (iter_response != response_bodys.end()) {
        response_body = iter_response->second;
      }
      if (nullptr == response_body) {
        response_body = ctx.create<::atfw::trade::SSTradeOrderFetchUnlockOrderRsp>();
      }
      if (nullptr == response_body) {
        FCTXLOGERROR(ctx, "malloc {} for server instance id {} failed", "SSTradeOrderFetchUnlockOrderRsp",
                     request_body.first);
        continue;
      }
    }

    dispatcher_await_options waiter_options = dispatcher_make_default<dispatcher_await_options>();
    int32_t res = RPC_AWAIT_CODE_RESULT(::rpc::trade_order::fetch_unlock(ctx, request_body.first, *request_body.second,
                                                                         *response_body, false, &waiter_options));
    if (res < 0) {
      FCTXLOGERROR(ctx, "Try to call fetch_unlock to server {}({:#x}) failed, res: {}({})", request_body.first,
                   request_body.first, res, protobuf_mini_dumper_get_error_msg(res));
      last_error_res = res;
    } else {
      waiters.emplace(std::move(waiter_options));
    }
  }

  if (!waiters.empty()) {
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, waiters, received));
    if (res < 0) {
      FCTXLOGERROR(ctx, "Try to wait multiple response failed, res: {}({})", res,
                   protobuf_mini_dumper_get_error_msg(res));
      last_error_res = res;
    }
  }
  for (auto& received_message : received) {
    if (received_message.second.body_bin().empty()) {
      continue;
    }

    auto iter_response = response_bodys.find(received_message.first);
    if (iter_response == response_bodys.end()) {
      FCTXLOGERROR(ctx, "Got unexpected response from {}({:#x})", received_message.first, received_message.first);
      continue;
    }
    if (iter_response->second == nullptr) {
      FCTXLOGERROR(ctx, "Got unexpected response from {}({:#x})", received_message.first, received_message.first);
      continue;
    }
    if (!::rpc::trade_order::packer::unpack_fetch_unlock(received_message.second.body_bin(), *iter_response->second)) {
      FCTXLOGERROR(ctx, "Unpack response from {}({:#x}) failed: {}", received_message.first, received_message.first,
                   iter_response->second->InitializationErrorString());
    }
  }

  RPC_RETURN_CODE(last_error_res);
}

TRADE_SDK_ORDER_API bool will_product_confirmation_reset_guid(
    const PROJECT_NAMESPACE_ID::config::ExcelTradeCommodity& cfg) {
  return !cfg.force_save_db();
}

}  // namespace trade
}  // namespace atframework
