// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#include "rpc/trade/search.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.trade.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/trade_search_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <trade/sharding.h>

#include <rpc/rpc_common_types.h>
#include <rpc/rpc_utils.h>
#include <rpc/trade_search/tradesearchservice.atfw.gen.h>

#include <utility/protobuf_mini_dumper.h>

#include <sstream>
#include <string>

namespace atframework {
namespace trade {

namespace {
static std::string collect_order_ticket_id(
    const google::protobuf::RepeatedPtrField<atfw::trade::trade_order_ticket_key>& order_keys) {
  std::stringstream ss;
  ss << "[";
  bool is_first = true;
  for (const auto& order_key : order_keys) {
    if (order_key.trade_order_ticket_id() <= 0) {
      continue;
    }

    if (!is_first) {
      ss << ", ";
    }
    ss << order_key.trade_order_ticket_id();
    is_first = false;
  }
  ss << "]";
  return ss.str();
}

static std::string collect_market_product_id(gsl::span<const atfw::trade::DTradeMarketProductKey> product_keys) {
  std::stringstream ss;
  ss << "[";
  bool is_first = true;
  for (const auto& product_key : product_keys) {
    if (product_key.trade_market_id() <= 0) {
      continue;
    }

    if (!is_first) {
      ss << ", ";
    }
    ss << "(" << product_key.trade_market_id() << ", " << product_key.trade_product_id() << ")";
    is_first = false;
  }
  ss << "]";
  return ss.str();
}
}  // namespace

TRADE_SDK_SEARCH_API rpc::result_code_type search(rpc::context& ctx, int32_t area_code, int32_t order_type,
                                                  atfw::trade::DTradeOrderTicketSearchRule&& query, int64_t count,
                                                  int32_t sort_by, atfw::trade::SSTradeSearchQueryRsp& response_body,
                                                  uint64_t random_seed) {
  rpc::context::message_holder<atfw::trade::SSTradeSearchQueryReq> req_body{ctx};

  req_body->set_area_code(area_code);
  req_body->set_order_type(order_type);
  req_body->set_count(count);
  req_body->set_random_seed(random_seed);
  req_body->set_sort_by(static_cast<atfw::trade::EnTradeSearchSortBy>(sort_by));

  req_body->set_slot_id(atfw::trade_api::get_search_random_slot(query, atfw::trade_api::EnSearchSlotType::kReplicate));
  req_body->set_ttl(0);

  if (req_body->slot_id() <= 0) {
    FWLOGERROR("search order area_code: {}, order_type: {}, query: {} failed, can not find available slot", area_code,
               order_type, protobuf_mini_dumper_get_readable(query));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  auto server_node = atfw::trade_api::get_server_node_of_search_slot(area_code, req_body->slot_id());
  if (!server_node) {
    FWLOGERROR(
        "search order area_code: {}, order_type: {}, query: {} failed, can not find available server for slot {}",
        area_code, order_type, protobuf_mini_dumper_get_readable(query), req_body->slot_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  protobuf_move_message(*req_body->mutable_query(), std::move(query));

  rpc::result_code_type::value_type ret =
      RPC_AWAIT_CODE_RESULT(rpc::trade_search::unicast::query(ctx, *server_node, *req_body, response_body));
  if (ret < 0) {
    FWLOGERROR("search order area_code: {}, order_type: {}, query: {} failed, result: {}({})", area_code, order_type,
               protobuf_mini_dumper_get_readable(query), ret, protobuf_mini_dumper_get_error_msg(ret));
  }
  RPC_RETURN_CODE(ret);
}

TRADE_SDK_SEARCH_API rpc::result_code_type get_orders(rpc::context& ctx,
                                                      gsl::span<const atfw::trade::trade_order_ticket_key> ticket_keys,
                                                      atfw::trade::SSTradeSearchGetOrderRsp& response_body) {
  // 先按area code分组
  std::unordered_map<int32_t, atfw::trade::SSTradeSearchGetOrderReq*> request_bodys;
  for (const auto& ticket_key : ticket_keys) {
    int32_t area_code = ticket_key.area_code();
    auto iter = request_bodys.find(area_code);
    if (iter != request_bodys.end() && nullptr != iter->second) {
      protobuf_copy_message(*iter->second->add_ticket_key(), ticket_key);
    } else {
      atfw::trade::SSTradeSearchGetOrderReq* request_body = ctx.create<atfw::trade::SSTradeSearchGetOrderReq>();
      if (nullptr != request_body) {
        request_body->set_slot_id(
            atfw::trade_api::get_search_random_slot(ticket_key, atfw::trade_api::EnSearchSlotType::kReplicate));
        request_body->set_ttl(0);

        if (request_body->slot_id() <= 0) {
          FWLOGERROR("get order {} (area_code: {}) failed, can not find available slot",
                     ticket_key.trade_order_ticket_id(), area_code);
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
        }

        protobuf_copy_message(*request_body->add_ticket_key(), ticket_key);
        request_bodys[area_code] = request_body;
      }
    }
  }

  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::EN_SUCCESS;

  // FIXME(owentou): 以后看需要改成并行请求
  for (auto& request_body_pair : request_bodys) {
    if (nullptr == request_body_pair.second) {
      continue;
    }

    int32_t area_code = request_body_pair.first;
    atfw::trade::SSTradeSearchGetOrderReq& request_body = *request_body_pair.second;
    auto server_node = atfw::trade_api::get_server_node_of_search_slot(area_code, request_body.slot_id());
    if (!server_node) {
      FWLOGERROR("get order {} (area_code: {}) failed, can not find available server",
                 collect_order_ticket_id(request_body.ticket_key()), area_code);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::trade_search::unicast::get_order(ctx, *server_node, request_body, response_body));
    if (ret < 0) {
      FWLOGERROR("get order {} (area_code: {}) failed, result: {}({})",
                 collect_order_ticket_id(request_body.ticket_key()), area_code, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
    }
  }

  RPC_RETURN_CODE(ret);
}

EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type batch_get_product_basic(
    rpc::context& ctx, int32_t order_type, gsl::span<const atfw::trade::DTradeMarketProductKey> product_keys,
    atfw::trade::SSTradeSearchBatchGetSalesBasicIndexRsp& response_body) {
  rpc::result_code_type::value_type ret = 0;
  if (product_keys.empty()) {
    RPC_RETURN_CODE(ret);
  }

  // 使用第一个计算分片
  int32_t slot_id = atfw::trade_api::get_search_random_slot(product_keys[0]);
  if (slot_id <= 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_PRODUCT_NOT_FOUND_IN_TABLE);
  }

  auto server_node = atfw::trade_api::get_server_node_of_search_slot(product_keys[0].area_code(), slot_id);
  if (!server_node) {
    FWLOGERROR("get order {} (area_code: {}) failed, can not find available server for slot {}",
               collect_market_product_id(product_keys), product_keys[0].area_code(), slot_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  rpc::context::message_holder<atfw::trade::SSTradeSearchBatchGetSalesBasicIndexReq> request_body{ctx};
  request_body->mutable_timestamp()->set_seconds(util::time::time_utility::get_now());
  request_body->mutable_trade_product_key()->Reserve(static_cast<int>(product_keys.size()));
  request_body->set_order_type(order_type);
  for (const auto& product_key : product_keys) {
    protobuf_copy_message(*request_body->add_trade_product_key(), product_key);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      rpc::trade_search::unicast::batch_get_sales_basic(ctx, *server_node, *request_body, response_body));
  if (ret < 0) {
    FWLOGERROR("get order {} (area_code: {}) from slot id {} failed, result: {}",
               collect_market_product_id(product_keys), product_keys[0].area_code(), slot_id, ret);
  }

  RPC_RETURN_CODE(ret);
}

EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type get_recommend_order_ticket(
    rpc::context& ctx, const atfw::trade::DTradeRecommendOrderTicketRule& query,
    atfw::trade::DTradeRecommendOrderTicketResult& result, uint64_t random_seed) {
  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::EN_SUCCESS;

  if (!query.has_recommend_score_rule() && !query.has_publish_new_rule() && !query.has_best_selling_amount_rule() &&
      !query.has_best_selling_price_increase_rule() && !query.has_rarity_custom_index_rule() &&
      query.batch_rule_size() <= 0) {
    RPC_RETURN_CODE(ret);
  }

  int32_t slot_id =
      atfw::trade_api::get_recommend_order_ticket_random_slot(query.trade_market_id(), query.order_ticket_type());
  if (slot_id <= 0) {
    FCTXLOGERROR(ctx, "recommend order {},{},{} failed, can not find available slot", query.area_code(),
                 query.order_ticket_type(), query.trade_market_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  auto server_node = atfw::trade_api::get_server_node_of_recommend_slot(query.area_code(), slot_id);
  if (!server_node) {
    FCTXLOGERROR(ctx, "recommend order {},{},{} failed, can not find available server for slot {}", query.area_code(),
                 query.order_ticket_type(), query.trade_market_id(), slot_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  rpc::context::message_holder<atfw::trade::SSTradeSearchGetRecommendOrderTicketReq> request_body{ctx};
  rpc::context::message_holder<atfw::trade::SSTradeSearchGetRecommendOrderTicketRsp> response_body{ctx};

  request_body->set_slot_id(slot_id);
  request_body->set_random_seed(random_seed);
  protobuf_copy_message(*request_body->mutable_query(), query);

  ret = RPC_AWAIT_CODE_RESULT(
      rpc::trade_search::unicast::get_recommend_order_ticket(ctx, *server_node, *request_body, *response_body));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = response_body->result_code();
  protobuf_move_message(result, std::move(*response_body->mutable_result()));

  RPC_RETURN_CODE(ret);
}

EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type get_recommend_product(
    rpc::context& ctx, atfw::trade::DTradeRecommendProductRule& query,
    atfw::trade::DTradeRecommendProductResult& result) {
  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::EN_SUCCESS;

  if (!query.has_hot_sale_rule() && !query.has_custom_search_rule() &&
      query.batch_rarity_custom_type_rule_size() <= 0) {
    RPC_RETURN_CODE(ret);
  }
  if (query.order_type() == 0) {
    query.set_order_type(static_cast<int32_t>(atfw::trade::DTradeOrderTicketMeta::kSalesOrder));
  }

  int32_t slot_id =
      atfw::trade_api::get_recommend_order_ticket_random_slot(query.trade_market_id(), query.order_type());
  if (slot_id <= 0) {
    FCTXLOGERROR(ctx, "recommend product {},{} failed, can not find available slot", query.area_code(),
                 query.trade_market_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  auto server_node = atfw::trade_api::get_server_node_of_recommend_slot(query.area_code(), slot_id);
  if (!server_node) {
    FCTXLOGERROR(ctx, "recommend product {},{} failed, can not find available server for slot {}", query.area_code(),
                 query.trade_market_id(), slot_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  rpc::context::message_holder<atfw::trade::SSTradeSearchGetRecommendProductReq> request_body{ctx};
  rpc::context::message_holder<atfw::trade::SSTradeSearchGetRecommendProductRsp> response_body{ctx};

  request_body->set_slot_id(slot_id);
  if (request_body->GetArena() == query.GetArena()) {
    if (nullptr == query.GetArena()) {
      request_body->set_allocated_query(&query);
    } else {
      request_body->unsafe_arena_set_allocated_query(&query);
    }
  } else {
    protobuf_copy_message(*request_body->mutable_query(), query);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      rpc::trade_search::unicast::get_recommend_product(ctx, *server_node, *request_body, *response_body));

  if (request_body->GetArena() == query.GetArena()) {
    if (nullptr == query.GetArena()) {
      ATFW_EXPLICIT_UNUSED_ATTR auto* _ = request_body->release_query();
    } else {
      ATFW_EXPLICIT_UNUSED_ATTR auto* _ = request_body->unsafe_arena_release_query();
    }
  }
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = response_body->result_code();
  protobuf_move_message(result, std::move(*response_body->mutable_result()));

  RPC_RETURN_CODE(ret);
}

EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type query_standard_price(
    rpc::context& ctx, atfw::trade::DTradeBatchQueryStandardPriceRule& query,
    atfw::trade::DTradeBatchQueryStandardPriceResult& result) {
  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::EN_SUCCESS;

  if (query.batch_query_standard_price_rule_size() <= 0) {
    RPC_RETURN_CODE(ret);
  }

  const auto& first_query = query.batch_query_standard_price_rule(0);

  int32_t slot_id = atfw::trade_api::get_recommend_order_ticket_random_slot(
      query.trade_market_id(), first_query.order_type(), atfw::trade_api::EnSearchSlotType::kReplicate);
  if (slot_id <= 0) {
    FCTXLOGERROR(ctx, "query standard price {},{} failed, can not find available slot", query.area_code(),
                 query.trade_market_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  auto server_node = atfw::trade_api::get_server_node_of_recommend_slot(query.area_code(), slot_id);
  if (!server_node) {
    FCTXLOGERROR(ctx, "query standard price {},{} failed, can not find available server for slot {}", query.area_code(),
                 query.trade_market_id(), slot_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE);
  }

  // 所有的请求组都要分配 callback_sequence
  int64_t max_callback_sequence = 0;
  for (const auto& query_item : query.batch_query_standard_price_rule()) {
    if (query_item.callback_sequence() > max_callback_sequence) {
      max_callback_sequence = query_item.callback_sequence();
    }
  }

  for (auto& query_item : *query.mutable_batch_query_standard_price_rule()) {
    if (query_item.callback_sequence() <= 0) {
      query_item.set_callback_sequence(++max_callback_sequence);
    }
  }

  rpc::context::message_holder<atfw::trade::SSTradeQueryAggregationStandardPriceReq> request_body{ctx};
  rpc::context::message_holder<atfw::trade::SSTradeQueryAggregationStandardPriceRsp> response_body{ctx};

  if (request_body->GetArena() == query.GetArena()) {
    if (nullptr == query.GetArena()) {
      request_body->set_allocated_query(&query);
    } else {
      request_body->unsafe_arena_set_allocated_query(&query);
    }
  } else {
    protobuf_copy_message(*request_body->mutable_query(), query);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      rpc::trade_search::unicast::query_aggregation_standard_price(ctx, *server_node, *request_body, *response_body));

  if (request_body->GetArena() == query.GetArena()) {
    if (nullptr == query.GetArena()) {
      ATFW_EXPLICIT_UNUSED_ATTR auto* _ = request_body->release_query();
    } else {
      ATFW_EXPLICIT_UNUSED_ATTR auto* _ = request_body->unsafe_arena_release_query();
    }
  }
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = response_body->result_code();
  protobuf_move_message(result, std::move(*response_body->mutable_result()));

  RPC_RETURN_CODE(ret);
}

}  // namespace trade
}  // namespace atframework
