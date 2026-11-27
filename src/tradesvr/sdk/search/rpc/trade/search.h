// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#pragma once

#include <config/compile_optimize.h>
#include <std/explicit_declare.h>

#include <gsl/select-gsl.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

#include <cstdint>

namespace rpc {
class context;
}

namespace atframework {
namespace trade {
class DTradeOrderTicketSearchRule;
class DTradeMarketProductKey;
class DTradeRecommendOrderTicketRule;
class DTradeRecommendOrderTicketResult;
class DTradeRecommendProductRule;
class DTradeRecommendProductResult;
class DTradeBatchQueryStandardPriceRule;
class DTradeBatchQueryStandardPriceResult;
class trade_order_ticket_key;
class SSTradeSearchQueryRsp;
class SSTradeSearchGetOrderRsp;
class SSTradeSearchBatchGetSalesBasicIndexRsp;
}  // namespace trade
}  // namespace atframework

namespace atframework {
namespace trade {

/**
 * @brief 搜索商品
 *
 * @param ctx RPC上下文
 * @param area_code 区域码
 * @param order_type 订单类型, 指向 @DTradeOrderTicketMeta.type
 * @param query 搜索请求
 * @param count 数量限制(不能超过最大上限)
 * @param sort_by 排序类型, 指向 @EnTradeSearchSortBy
 * @param response_body 输出回包
 * @return 0或错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type search(
    rpc::context& ctx, int32_t area_code, int32_t order_type, atframework::trade::DTradeOrderTicketSearchRule&& query,
    int64_t count, int32_t sort_by, atframework::trade::SSTradeSearchQueryRsp& response_body, uint64_t random_seed);

/**
 * @brief 拉取委托单详情
 *
 * @param ctx RPC上下文
 * @param ticket_keys 委托单Key
 * @param response_body 本次查询中成功区域的委托单详情和未找到的 Key
 * @return 全部区域成功时返回 0，否则返回首个错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type get_orders(
    rpc::context& ctx, gsl::span<const atfw::trade::trade_order_ticket_key> ticket_keys,
    atframework::trade::SSTradeSearchGetOrderRsp& response_body);

/**
 * @brief 拉取商品详情
 *
 * @param ctx RPC上下文
 * @param order_type 订单类型
 * @param product_key 商品Key
 * @param response_body 输出回包
 * @return 0或错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type batch_get_product_basic(
    rpc::context& ctx, int32_t order_type, gsl::span<const atframework::trade::DTradeMarketProductKey> product_key,
    atframework::trade::SSTradeSearchBatchGetSalesBasicIndexRsp& response_body);

/**
 * @brief 拉取推荐的委托单（拍卖）
 *
 * @param ctx RPC上下文
 * @param query 拉取条件
 * @param result 输出回包
 * @return 0或错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type get_recommend_order_ticket(
    rpc::context& ctx, const atframework::trade::DTradeRecommendOrderTicketRule& query,
    atframework::trade::DTradeRecommendOrderTicketResult& result, uint64_t random_seed);

/**
 * @brief 拉取推荐的商品单（同质化）
 *
 * @param ctx RPC上下文
 * @param query 拉取条件
 * @param result 输出回包
 * @return 0或错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type get_recommend_product(
    rpc::context& ctx, atframework::trade::DTradeRecommendProductRule& query,
    atframework::trade::DTradeRecommendProductResult& result);

/**
 * @brief 拉取基准价格请求
 *
 * @param ctx RPC上下文
 * @param query 拉取条件
 * @param result 输出回包
 * @return 0或错误码
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_SEARCH_API rpc::result_code_type query_standard_price(
    rpc::context& ctx, atframework::trade::DTradeBatchQueryStandardPriceRule& query,
    atframework::trade::DTradeBatchQueryStandardPriceResult& result);

}  // namespace trade
}  // namespace atframework
