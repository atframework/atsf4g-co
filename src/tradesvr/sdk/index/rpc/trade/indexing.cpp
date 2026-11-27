// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#include "rpc/trade/indexing.h"

#include <config/compile_optimize.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.trade.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.trade.pb.h>
#include <protocol/pbdesc/trade_index_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <logic/hpa/logic_hpa_easy_api.h>

#include <dispatcher/dispatcher_type_defines.h>
#include <rpc/rpc_common_types.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <trade/sharding.h>

#include <cstdint>
#include <unordered_map>

#include "rpc/trade_index/tradeindexservice.atfw.gen.h"
#include "utility/protobuf_mini_dumper.h"

namespace atframework {
namespace trade {

namespace {
struct ATFW_UTIL_SYMBOL_LOCAL rpc_group_t {
  uint64_t server_id;
  atfw::trade::SSTradeBatchGetBasicIndexReq* req;
  atfw::trade::SSTradeBatchGetBasicIndexRsp* rsp;
};
}  // namespace

TRADE_SDK_INDEX_API rpc::result_code_type index_fetch_product_histogram_distribution(
    rpc::context& ctx, int32_t order_type, const atfw::trade::DTradeMarketProductKey& product_key,
    atfw::trade::SSTradeIndexGetRsp& rsp_body) {
  int32_t ret = PROJECT_NAMESPACE_ID::EN_SUCCESS;

  do {
    int32_t slot_id = atfw::trade_api::get_index_random_slot(product_key, atfw::trade_api::EnIndexSlotType::kReplicate);
    if (slot_id <= 0) {
      FWLOGERROR("get index random slot failed, product (area_code {}, market_id {}, product_id {}), slot_id {}",
                 product_key.area_code(), product_key.trade_market_id(), product_key.trade_product_id(), slot_id);
      ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      break;
    }

    auto server_node = atfw::trade_api::get_server_node_of_index_slot(product_key.area_code(), slot_id,
                                                                      logic_hpa_discovery_select_mode::kReady);
    if (!server_node) {
      FWLOGERROR("cannot find server node of index service by slot_id {}, area_code {}", slot_id,
                 product_key.area_code());
      ret = PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE;
      break;
    }
    uint64_t server_id = server_node->get_discovery_info().id();
    rpc::context::message_holder<atfw::trade::SSTradeIndexGetReq> req_body{ctx};
    // set info value in request body
    req_body->set_slot_id(slot_id);
    req_body->set_ttl(0);

    auto req_order_type = order_type;
    if (req_order_type == 0) {
      req_order_type = static_cast<int32_t>(atfw::trade::DTradeOrderTicketMeta::kSalesOrder);
    }
    req_body->set_order_type(req_order_type);
    protobuf_copy_message(*req_body->mutable_trade_product_key(), product_key);
    auto now = util::time::time_utility::now();
    req_body->mutable_timestamp()->set_seconds(std::chrono::system_clock::to_time_t(now));
    req_body->mutable_timestamp()->set_nanos(
        static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 now - std::chrono::system_clock::from_time_t(req_body->timestamp().seconds()))
                                 .count()));
    protobuf_copy_message(*rsp_body.mutable_info()->mutable_trade_product_key(), product_key);
    rsp_body.set_slot_id(slot_id);
    rsp_body.set_order_type(order_type);

    ret = RPC_AWAIT_CODE_RESULT(
        rpc::trade_index::fetch_product_histogram_distribution(ctx, server_id, *req_body, rsp_body));
    if (ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWLOGERROR("PRODUCT_HISTOGRAM rpc get_index require failed, server_id {}, area_code {}, errcode {}({})",
                 server_id, product_key.area_code(), ret, protobuf_mini_dumper_get_error_msg(ret));
    }

    FWLOGDEBUG("PRODUCT_HISTOGRAM index rsp {}", rsp_body.DebugString());

  } while (false);

  RPC_RETURN_CODE(ret);
}

TRADE_SDK_INDEX_API rpc::result_code_type index_batch_fetch_product_distribution(
    rpc::context& ctx, int32_t order_type,
    const google::protobuf::RepeatedPtrField<atfw::trade::DTradeMarketProductKey>& product_keys,
    atfw::trade::SSTradeBatchGetBasicIndexRsp& rsp_src) {
  auto now = util::time::time_utility::get_now();
  std::list<rpc_group_t> send_list;
  // <server_id, <index_slot_id, {product_key}>>
  std::unordered_map<uint64_t, std::unordered_map<int32_t, std::vector<atfw::trade::DTradeMarketProductKey>>>
      server_map;

  auto req_order_type = order_type;
  if (req_order_type == 0) {
    req_order_type = static_cast<int32_t>(atfw::trade::DTradeOrderTicketMeta::kSalesOrder);
  }
  for (const auto& product : product_keys) {
    FWLOGDEBUG("PRODUCT_DISTRIBUTION batch_basic_index product {}", product.DebugString());

    int32_t slot_id = atfw::trade_api::get_index_random_slot(product, atfw::trade_api::EnIndexSlotType::kReplicate);
    if (slot_id <= 0) {
      FWLOGERROR("PRODUCT_DISTRIBUTION cannot find index slot by product key {}",
                 protobuf_mini_dumper_get_readable(product));
      continue;
    }
    auto server_node = atfw::trade_api::get_server_node_of_index_slot(product.area_code(), slot_id,
                                                                      logic_hpa_discovery_select_mode::kReady);
    if (!server_node) {
      FWLOGERROR("PRODUCT_DISTRIBUTION cannot find server node of index service by slot_id {}, area_code {}", slot_id,
                 product.area_code());
      continue;
    }

    uint64_t server_id = server_node->get_discovery_info().id();
    auto& product_list = server_map[server_id][slot_id];
    product_list.push_back(product);
  }

  for (auto& slot_map : server_map) {
    auto server_id = slot_map.first;
    for (auto& slot_id_product_info : slot_map.second) {
      rpc::context::message_holder<atfw::trade::SSTradeBatchGetBasicIndexReq> rpc_req{ctx};
      rpc::context::message_holder<atfw::trade::SSTradeBatchGetBasicIndexRsp> rpc_rsp{ctx};
      rpc_req->set_slot_id(slot_id_product_info.first);
      rpc_req->mutable_timestamp()->set_seconds(now);
      rpc_req->set_order_type(req_order_type);
      for (auto& product : slot_id_product_info.second) {
        protobuf_copy_message(*rpc_req->add_trade_product_key(), product);
      }

      // rsp
      rpc_rsp->set_slot_id(slot_id_product_info.first);
      rpc_rsp->set_order_type(order_type);

      // 重构后新接口
      int32_t res =
          RPC_AWAIT_CODE_RESULT(rpc::trade_index::batch_fetch_product_distribution(ctx, server_id, *rpc_req, *rpc_rsp));
      if (res != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
        FWLOGERROR(
            "PRODUCT_DISTRIBUTION try to call rpc::trade::batch_fetch_product_distribution to server {:#x} failed, "
            "errcode {}({})",
            server_id, res, protobuf_mini_dumper_get_error_msg(res));
        RPC_RETURN_CODE(res);
      }
      auto local_result_code = rpc_rsp->result_code();
      if (local_result_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
        rsp_src.set_result_code(local_result_code);
        RPC_RETURN_CODE(local_result_code);
      }
      // merge info in rsp_src
      for (const auto& rsp_info : rpc_rsp->info()) {
        auto* info = rsp_src.add_info();
        if (info == nullptr) {
          FWLOGERROR("PRODUCT_DISTRIBUTION malloc rsp_body info in batch_get_basic_index failed");
          continue;
        }
        protobuf_copy_message(*info, rsp_info);
      }
      rsp_src.set_order_type(rpc_rsp->order_type());
    }
  }
  FWLOGDEBUG("PRODUCT_DISTRIBUTION batch_basic_index rsp {}", rsp_src.DebugString());

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
}

TRADE_SDK_INDEX_API rpc::result_code_type index_fetch_price_permille(
    rpc::context& ctx, const atfw::trade::DTradeMarketProductKey& product_key,
    atfw::trade::SSTradeGetProductPermilleRsp& rsp_body) {
  int32_t ret = PROJECT_NAMESPACE_ID::EN_SUCCESS;

  do {
    int32_t slot_id = atfw::trade_api::get_index_random_slot(product_key, atfw::trade_api::EnIndexSlotType::kReplicate);
    if (slot_id <= 0) {
      FWLOGERROR("get index random slot failed, product (area_code {}, market_id {}, product_id {}), slot_id {}",
                 product_key.area_code(), product_key.trade_market_id(), product_key.trade_product_id(), slot_id);
      ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      break;
    }

    auto server_node = atfw::trade_api::get_server_node_of_index_slot(product_key.area_code(), slot_id,
                                                                      logic_hpa_discovery_select_mode::kReady);
    if (!server_node) {
      FWLOGERROR("cannot find server node of index service by slot_id {}, area_code {}", slot_id,
                 product_key.area_code());
      ret = PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE;
      break;
    }

    uint64_t server_id = server_node->get_discovery_info().id();
    rpc::context::message_holder<atfw::trade::SSTradeGetProductPermilleReq> req_body{ctx};
    // set info value in request body
    req_body->set_slot_id(slot_id);
    req_body->set_ttl(0);
    protobuf_copy_message(*req_body->mutable_trade_product_key(), product_key);

    auto now = util::time::time_utility::now();
    req_body->mutable_timestamp()->set_seconds(std::chrono::system_clock::to_time_t(now));
    req_body->mutable_timestamp()->set_nanos(
        static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 now - std::chrono::system_clock::from_time_t(req_body->timestamp().seconds()))
                                 .count()));

    ret = RPC_AWAIT_CODE_RESULT(rpc::trade_index::fetch_product_price_permille(ctx, server_id, *req_body, rsp_body));
    if (ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWLOGERROR("PRODUCT_PRICE_PERMILLE rpc get_index require failed, server_id {}, area_code {}, errcode {}({})",
                 server_id, product_key.area_code(), ret, protobuf_mini_dumper_get_error_msg(ret));
    }

    FWLOGDEBUG("PRODUCT_PRICE_PERMILLE index rsp {}", rsp_body.DebugString());

  } while (false);

  RPC_RETURN_CODE(ret);
}

}  // namespace trade
}  // namespace atframework
