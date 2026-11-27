// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#pragma once

#include <config/compile_optimize.h>

#include <config/server_frame_build_feature.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.trade.common.pb.h>
#include <protocol/pbdesc/com.struct.trade.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdint>

namespace atframework {
namespace trade_search {
/**
 * @brief 获取指定商品的搜索服务的随机 Slot ID
 *
 * @param search_rule 订购单商品信息
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_SEARCH_API int32_t get_random_slot_id(const atframework::trade::DTradeOrderTicketSearchRule& search_rule);

/**
 * @brief 获取指定委托单的搜索服务的随机 Slot ID
 *
 * @param order_ticket_key 委托单Key
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_SEARCH_API int32_t get_random_slot_id(const atfw::trade::trade_order_ticket_key& order_ticket_key);

/**
 * @brief 获取指定商品的搜索服务的随机 Slot ID
 *
 * @param product_key 商品Key
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_SEARCH_API int32_t get_random_slot_id(const atframework::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取指定商品的搜索价格
 *
 * @param meta 商品Meta
 * @return 指定商品的搜索价格
 */
TRADE_SDK_SEARCH_API int64_t get_search_price(const atframework::trade::DTradeOrderTicketMeta& meta) noexcept;

}  // namespace trade_search
}  // namespace atframework
