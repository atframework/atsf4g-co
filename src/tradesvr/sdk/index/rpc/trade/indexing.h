// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#pragma once

#include <config/compile_optimize.h>

#include <config/server_frame_build_feature.h>
#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/repeated_field.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <std/explicit_declare.h>

namespace rpc {
class context;
}  // namespace rpc

namespace atframework {
namespace trade {
class DTradeMarketProductKey;

class SSTradeIndexGetRsp;
class SSTradeBatchGetBasicIndexRsp;
class SSTradeGetProductPermilleRsp;
}  // namespace trade
}  // namespace atframework

namespace atframework {
namespace trade {
ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_INDEX_API rpc::result_code_type index_fetch_product_histogram_distribution(
    rpc::context& ctx, int32_t order_type, const atframework::trade::DTradeMarketProductKey& product_key,
    atframework::trade::SSTradeIndexGetRsp& rsp_body);

ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_INDEX_API rpc::result_code_type index_batch_fetch_product_distribution(
    rpc::context& ctx, int32_t order_type,
    const google::protobuf::RepeatedPtrField<atframework::trade::DTradeMarketProductKey>& product_keys,
    atframework::trade::SSTradeBatchGetBasicIndexRsp& rsp_body);

ATFW_EXPLICIT_NODISCARD_ATTR TRADE_SDK_INDEX_API rpc::result_code_type index_fetch_price_permille(
    rpc::context& ctx, const atframework::trade::DTradeMarketProductKey& product_key,
    atframework::trade::SSTradeGetProductPermilleRsp& rsp_body);
}  // namespace trade
}  // namespace atframework
