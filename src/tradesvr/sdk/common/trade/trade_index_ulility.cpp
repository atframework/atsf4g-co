// Copyright 2026 atframework
// Created by owent on 2026-11-26.
//

#include "trade/trade_index_ulility.h"

#include <xxhash.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#ifdef max
#  undef max
#endif
#ifdef min
#  undef min
#endif

namespace atframework {

namespace {
template <class T>
inline static void copy_buffer_into(char* start, size_t offset, T value) {
  memcpy(start + offset, &value, sizeof(value));
}
}  // namespace
namespace trade_api {

TRADE_SDK_COMMON_API int64_t get_order_ticket_current_price(const atfw::trade::DTradeOrderTicketMeta& meta) {
  switch (meta.type_case()) {
    case atfw::trade::DTradeOrderTicketMeta::kSalesOrder:
      return meta.sales_order().unit_price();
      break;
    case atfw::trade::DTradeOrderTicketMeta::kPurchaseOrder:
      return meta.purchase_order().current_unit_price();
      break;
    case atfw::trade::DTradeOrderTicketMeta::kAuctionOrder:
      return std::max(meta.auction_order().auction_order_meta().origin_price(),
                      meta.auction_order().auction_order_meta().current_price());
      break;
    default:
      break;
  }

  return -1;
}

TRADE_SDK_COMMON_API int64_t get_order_ticket_fixed_price(const atfw::trade::DTradeOrderTicketMeta& meta) {
  switch (meta.type_case()) {
    case atfw::trade::DTradeOrderTicketMeta::kSalesOrder:
      return meta.sales_order().unit_price();
      break;
    case atfw::trade::DTradeOrderTicketMeta::kPurchaseOrder:
      return meta.purchase_order().current_unit_price();
      break;
    case atfw::trade::DTradeOrderTicketMeta::kAuctionOrder:
      return meta.auction_order().auction_order_meta().fixed_price();
      break;
    default:
      break;
  }

  return -1;
}

TRADE_SDK_COMMON_API int64_t get_order_ticket_price(atfw::trade::EnTradePriceMode price_mode,
                                                    const atfw::trade::DTradeOrderTicketMeta& meta) {
  if (atfw::trade::EN_TRADE_PRICE_MODE_FIXED == price_mode) {
    return get_order_ticket_fixed_price(meta);
  }

  return get_order_ticket_current_price(meta);
}

TRADE_SDK_COMMON_API bool trade_order_ticket_price_index_key::is_valid() const noexcept {
  return price >= 0 && order_ticket_id > 0;
}

TRADE_SDK_COMMON_API const trade_order_ticket_price_index_key&
trade_order_ticket_price_index_key::get_invalid_instance() noexcept {
  static trade_order_ticket_price_index_key invalid{atfw::trade::EN_TRADE_PRICE_MODE_CURRENT,
                                                    std::numeric_limits<int64_t>::min(), 0};
  return invalid;
}

TRADE_SDK_COMMON_API trade_order_ticket_price_index_key
trade_order_ticket_price_index_key::min(atfw::trade::EnTradePriceMode input_price_type) noexcept {
  return trade_order_ticket_price_index_key{input_price_type, 0, 1};
}

TRADE_SDK_COMMON_API trade_order_ticket_price_index_key
trade_order_ticket_price_index_key::max(atfw::trade::EnTradePriceMode input_price_type) noexcept {
  return trade_order_ticket_price_index_key{input_price_type, std::numeric_limits<int64_t>::max(),
                                            std::numeric_limits<int64_t>::max()};
}

TRADE_SDK_COMMON_API bool trade_order_ticket_price_index_key::less(
    const trade_order_ticket_price_index_key& l, const trade_order_ticket_price_index_key& r) noexcept {
  if (l.price_type != r.price_type) {
    return l.price_type < r.price_type;
  }
  if (l.price != r.price) {
    return l.price < r.price;
  }

  return l.order_ticket_id < r.order_ticket_id;
}

TRADE_SDK_COMMON_API bool trade_order_ticket_price_index_key::equal(
    const trade_order_ticket_price_index_key& l, const trade_order_ticket_price_index_key& r) noexcept {
  return l.price_type == r.price_type && l.price == r.price && l.order_ticket_id == r.order_ticket_id;
}

TRADE_SDK_COMMON_API size_t trade_order_ticket_price_index_key::hash_type::operator()(
    const trade_order_ticket_price_index_key& input) const noexcept {
  char hash_buffer[sizeof(int64_t) * 3];
  copy_buffer_into(hash_buffer, 0, static_cast<int64_t>(input.price_type));
  copy_buffer_into(hash_buffer, sizeof(int64_t), input.price);
  copy_buffer_into(hash_buffer, sizeof(int64_t) * 2, input.order_ticket_id);
  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), 803000031));
}

TRADE_SDK_COMMON_API bool trade_order_ticket_recommend_score_index_key::is_valid() const noexcept {
  return recommend_score != std::numeric_limits<int64_t>::min() && price >= 0 && order_ticket_id > 0;
}

TRADE_SDK_COMMON_API const trade_order_ticket_recommend_score_index_key&
trade_order_ticket_recommend_score_index_key::get_invalid_instance() noexcept {
  static trade_order_ticket_recommend_score_index_key invalid{
      std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::min(), 0, 0,
      atfw::trade::EnTradePriceMode::EN_TRADE_PRICE_MODE_CURRENT};
  return invalid;
}

TRADE_SDK_COMMON_API size_t trade_order_ticket_recommend_score_index_key::hash_type::operator()(
    const trade_order_ticket_recommend_score_index_key& input) const noexcept {
  char hash_buffer[sizeof(int64_t) * 4];
  copy_buffer_into(hash_buffer, 0, input.recommend_score);
  copy_buffer_into(hash_buffer, sizeof(int64_t), input.price);
  copy_buffer_into(hash_buffer, sizeof(int64_t) * 2, input.order_ticket_id);
  copy_buffer_into(hash_buffer, sizeof(int64_t) * 3, static_cast<int64_t>(input.rarity));
  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), 803000031));
}

TRADE_SDK_COMMON_API trade_order_ticket_recommend_score_index_key
trade_order_ticket_recommend_score_index_key::min(atfw::trade::EnTradePriceMode input_price_type) noexcept {
  return trade_order_ticket_recommend_score_index_key{std::numeric_limits<int64_t>::max(), 0, 0,
                                                      std::numeric_limits<int32_t>::max(), input_price_type};
}

TRADE_SDK_COMMON_API trade_order_ticket_recommend_score_index_key
trade_order_ticket_recommend_score_index_key::max(atfw::trade::EnTradePriceMode input_price_type) noexcept {
  return trade_order_ticket_recommend_score_index_key{
      std::numeric_limits<int64_t>::min() + 1, std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
      std::numeric_limits<int32_t>::min(), input_price_type};
}

TRADE_SDK_COMMON_API bool trade_order_ticket_recommend_score_index_key::less(
    const trade_order_ticket_recommend_score_index_key& l,
    const trade_order_ticket_recommend_score_index_key& r) noexcept {
  int64_t lsp = l.recommend_score == std::numeric_limits<int64_t>::min() ? std::numeric_limits<int32_t>::max()
                                                                         : l.recommend_score;
  int64_t rsp = r.recommend_score == std::numeric_limits<int64_t>::min() ? std::numeric_limits<int32_t>::max()
                                                                         : r.recommend_score;
  if (lsp != rsp) {
    return l.recommend_score > r.recommend_score;
  }

  if (l.rarity != r.rarity) {
    return l.rarity > r.rarity;
  }

  if (l.price != r.price) {
    return l.price < r.price;
  }

  if (l.price_mode != r.price_mode) {
    return l.price_mode < r.price_mode;
  }

  return l.order_ticket_id < r.order_ticket_id;
}

TRADE_SDK_COMMON_API bool trade_order_ticket_recommend_score_index_key::equal(
    const trade_order_ticket_recommend_score_index_key& l,
    const trade_order_ticket_recommend_score_index_key& r) noexcept {
  return l.recommend_score == r.recommend_score && l.price == r.price && l.rarity == r.rarity &&
         l.order_ticket_id == r.order_ticket_id && l.price_mode == r.price_mode;
}

TRADE_SDK_COMMON_API bool operator==(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  return atfw::trade_api::trade_order_ticket_price_index_key::equal(l, r);
}

TRADE_SDK_COMMON_API bool operator==(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  return atfw::trade_api::trade_order_ticket_recommend_score_index_key::equal(l, r);
}

#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
TRADE_SDK_COMMON_API std::strong_ordering operator<=>(
    const atfw::trade_api::trade_order_ticket_price_index_key& l,
    const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  if (atfw::trade_api::trade_order_ticket_price_index_key::less(l, r)) {
    return std::strong_ordering::less;
  }
  if (atfw::trade_api::trade_order_ticket_price_index_key::less(r, l)) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

TRADE_SDK_COMMON_API std::strong_ordering operator<=>(
    const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
    const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  if (atfw::trade_api::trade_order_ticket_recommend_score_index_key::less(l, r)) {
    return std::strong_ordering::less;
  }
  if (atfw::trade_api::trade_order_ticket_recommend_score_index_key::less(r, l)) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

#else
TRADE_SDK_COMMON_API bool operator!=(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_price_index_key::equal(l, r);
}

TRADE_SDK_COMMON_API bool operator<(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                    const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  return atfw::trade_api::trade_order_ticket_price_index_key::less(l, r);
}

TRADE_SDK_COMMON_API bool operator<=(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_price_index_key::less(r, l);
}

TRADE_SDK_COMMON_API bool operator>(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                    const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  return atfw::trade_api::trade_order_ticket_price_index_key::less(r, l);
}

TRADE_SDK_COMMON_API bool operator>=(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_price_index_key::less(l, r);
}

TRADE_SDK_COMMON_API bool operator!=(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_recommend_score_index_key::equal(l, r);
}

TRADE_SDK_COMMON_API bool operator<(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
                                    const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  return atfw::trade_api::trade_order_ticket_recommend_score_index_key::less(l, r);
}

TRADE_SDK_COMMON_API bool operator<=(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_recommend_score_index_key::less(r, l);
}

TRADE_SDK_COMMON_API bool operator>(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
                                    const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  return atfw::trade_api::trade_order_ticket_recommend_score_index_key::less(r, l);
}

TRADE_SDK_COMMON_API bool operator>=(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
                                     const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_recommend_score_index_key::less(l, r);
}
#endif

bool trade_order_ticket_recommend_single_price_value::is_valid() const {
  if (min_price <= 0) {
    return false;
  }
  if (*this == trade_order_ticket_recommend_single_price_value::max()) {
    return false;
  }
  return true;
}

TRADE_SDK_COMMON_API bool trade_order_ticket_recommend_single_price_value::need_update(
    const trade_order_ticket_recommend_single_price_value& previous,
    const trade_order_ticket_recommend_single_price_value& current) {
  return less(current, previous);
}

TRADE_SDK_COMMON_API trade_order_ticket_recommend_single_price_value
trade_order_ticket_recommend_single_price_value::min() noexcept {
  trade_order_ticket_recommend_single_price_value ret;
  ret.has_order = true;
  ret.min_price = std::numeric_limits<decltype(ret.min_price)>::min();
  return ret;
}

TRADE_SDK_COMMON_API trade_order_ticket_recommend_single_price_value
trade_order_ticket_recommend_single_price_value::max() noexcept {
  trade_order_ticket_recommend_single_price_value ret;
  ret.has_order = false;
  ret.min_price = std::numeric_limits<decltype(ret.min_price)>::max();
  return ret;
}

TRADE_SDK_COMMON_API bool trade_order_ticket_recommend_single_price_value::less(
    const trade_order_ticket_recommend_single_price_value& l,
    const trade_order_ticket_recommend_single_price_value& r) noexcept {
  // 有上架优先
  if (l.has_order != r.has_order) {
    return l.has_order;
  }

  return l.min_price < r.min_price;
}

TRADE_SDK_COMMON_API bool trade_order_ticket_recommend_single_price_value::equal(
    const trade_order_ticket_recommend_single_price_value& l,
    const trade_order_ticket_recommend_single_price_value& r) noexcept {
  return l.has_order == r.has_order && l.min_price == r.min_price;
}

TRADE_SDK_COMMON_API bool operator==(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  return atfw::trade_api::trade_order_ticket_recommend_single_price_value::equal(l, r);
}

#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
TRADE_SDK_COMMON_API std::strong_ordering operator<=>(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  if (atfw::trade_api::trade_order_ticket_recommend_single_price_value::less(l, r)) {
    return std::strong_ordering::less;
  }
  if (atfw::trade_api::trade_order_ticket_recommend_single_price_value::less(r, l)) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

#else
TRADE_SDK_COMMON_API bool operator!=(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_recommend_single_price_value::equal(l, r);
}

TRADE_SDK_COMMON_API bool operator<(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  return atfw::trade_api::trade_order_ticket_recommend_single_price_value::less(l, r);
}

TRADE_SDK_COMMON_API bool operator<=(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_recommend_single_price_value::less(r, l);
}

TRADE_SDK_COMMON_API bool operator>(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  return atfw::trade_api::trade_order_ticket_recommend_single_price_value::less(r, l);
}

TRADE_SDK_COMMON_API bool operator>=(
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
    const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept {
  return !atfw::trade_api::trade_order_ticket_recommend_single_price_value::less(l, r);
}

#endif

}  // namespace trade_api
}  // namespace atframework
