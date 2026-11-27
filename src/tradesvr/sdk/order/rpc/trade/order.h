// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#pragma once

#include <config/compile_optimize.h>
#include <std/explicit_declare.h>

#include <gsl/select-gsl.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

#include <stdint.h>
#include <unordered_map>

namespace rpc {
class context;
}

namespace atframework {
namespace trade {
class SSTradeOrderFetchLockOrderReq;
class SSTradeOrderFetchLockOrderRsp;
class SSTradeOrderFetchUnlockOrderReq;
class SSTradeOrderFetchUnlockOrderRsp;
}  // namespace trade
}  // namespace atframework

PROJECT_NAMESPACE_BEGIN
namespace config {
class ExcelTradeCommodity;
}  // namespace config
PROJECT_NAMESPACE_END

namespace atframework {
namespace trade {
/**
 * @brief 尝试加Fetch锁（Batch）
 *
 * @param ctx RPC上下文
 * @param request_bodys  请求包集合,Key为目标server id。
 *                       Value可以由 ctx.create<SSTradeOrderFetchLockOrderReq>() 或
 *                       rpc::context::message_holder<SSTradeOrderFetchLockOrderReq> 创建
 * @param response_bodys 按目标 server id 保存回包。复用已有对象，缺失或为空时由 ctx 创建，生命周期限于当前任务。
 * @return 全部成功时返回 0。部分请求失败时返回错误码，已收到的有效回包仍可读取。
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_ORDER_API rpc::result_code_type batch_try_fetch_lock(
    rpc::context& ctx,
    std::unordered_map<uint64_t, ::atframework::trade::SSTradeOrderFetchLockOrderReq*>& request_bodys,
    std::unordered_map<uint64_t, ::atframework::trade::SSTradeOrderFetchLockOrderRsp*>& response_bodys);

/**
 * @brief 解除Fetch锁（Batch）
 *
 * @param ctx RPC上下文
 * @param request_bodys 请求包集合，Key 为目标 server id。
 *                      Value 可以由 ctx.create<SSTradeOrderFetchUnlockOrderReq>() 或
 *                      rpc::context::message_holder<SSTradeOrderFetchUnlockOrderReq> 创建
 * @param response_bodys 按目标 server id 保存回包。复用已有对象，缺失或为空时由 ctx 创建，生命周期限于当前任务。
 * @return 全部成功时返回 0。部分请求失败时返回错误码，已收到的有效回包仍可读取。
 */
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_ORDER_API rpc::result_code_type batch_fetch_unlock(
    rpc::context& ctx,
    std::unordered_map<uint64_t, ::atframework::trade::SSTradeOrderFetchUnlockOrderReq*>& request_bodys,
    std::unordered_map<uint64_t, ::atframework::trade::SSTradeOrderFetchUnlockOrderRsp*>& response_bodys);

/**
 * @brief 判定目标商品成交时是否一定重置GUID
 *
 * @param cfg 配置
 *
 * @return 0或错误码
 */
TRADE_SDK_ORDER_API bool will_product_confirmation_reset_guid(
    const PROJECT_NAMESPACE_ID::config::ExcelTradeCommodity& cfg);
}  // namespace trade
}  // namespace atframework
