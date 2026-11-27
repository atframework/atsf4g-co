// Copyright 2026 atframework
// Created by owent on 2026-11-26.
//

#pragma once

#include <config/compile_optimize.h>

#include <nostd/function_ref.h>

#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.trade.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <cstdint>

namespace atframework {
namespace trade_api {

TRADE_SDK_COMMON_API int64_t get_order_ticket_current_price(const atfw::trade::DTradeOrderTicketMeta& meta);

TRADE_SDK_COMMON_API int64_t get_order_ticket_fixed_price(const atfw::trade::DTradeOrderTicketMeta& meta);

TRADE_SDK_COMMON_API int64_t get_order_ticket_price(atfw::trade::EnTradePriceMode price_mode,
                                                    const atfw::trade::DTradeOrderTicketMeta& meta);

struct ATFW_UTIL_SYMBOL_VISIBLE trade_order_ticket_price_index_key {
  atfw::trade::EnTradePriceMode price_type;
  int64_t price;
  int64_t order_ticket_id;

  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key() { *this = get_invalid_instance(); }
  ATFW_UTIL_FORCEINLINE ~trade_order_ticket_price_index_key() {}

  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key(atfw::trade::EnTradePriceMode input_price_type,
                                                           int64_t input_price, int64_t input_order_ticket_id) noexcept
      : price_type(input_price_type), price(input_price), order_ticket_id(input_order_ticket_id) {}
  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key(
      atfw::trade::EnTradePriceMode input_price_type,
      const atfw::trade::DTradeOrderTicketSearchBound& bound_exclude) noexcept
      : price_type(input_price_type),
        price(bound_exclude.price()),
        order_ticket_id(bound_exclude.trade_order_ticket_id()) {}
  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key(atfw::trade::EnTradePriceMode input_price_type,
                                                           const atfw::trade::DTradeOrderTicketMeta& meta) noexcept
      : price_type(input_price_type),
        price(get_order_ticket_price(input_price_type, meta)),
        order_ticket_id(meta.trade_order_ticket_id()) {}

  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key(const trade_order_ticket_price_index_key&) = default;
  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key(trade_order_ticket_price_index_key&&) = default;
  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key& operator=(const trade_order_ticket_price_index_key&) =
      default;
  ATFW_UTIL_FORCEINLINE trade_order_ticket_price_index_key& operator=(trade_order_ticket_price_index_key&&) = default;

  TRADE_SDK_COMMON_API bool is_valid() const noexcept;

  TRADE_SDK_COMMON_API static const trade_order_ticket_price_index_key& get_invalid_instance() noexcept;

  TRADE_SDK_COMMON_API static trade_order_ticket_price_index_key min(
      atfw::trade::EnTradePriceMode input_price_type) noexcept;
  TRADE_SDK_COMMON_API static trade_order_ticket_price_index_key max(
      atfw::trade::EnTradePriceMode input_price_type) noexcept;

  TRADE_SDK_COMMON_API static bool less(const trade_order_ticket_price_index_key& l,
                                        const trade_order_ticket_price_index_key& r) noexcept;
  TRADE_SDK_COMMON_API static bool equal(const trade_order_ticket_price_index_key& l,
                                         const trade_order_ticket_price_index_key& r) noexcept;

  struct hash_type {
    TRADE_SDK_COMMON_API size_t operator()(const trade_order_ticket_price_index_key&) const noexcept;
  };

  friend TRADE_SDK_COMMON_API bool operator==(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                              const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;
#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
  friend TRADE_SDK_COMMON_API std::strong_ordering operator<=>(
      const atfw::trade_api::trade_order_ticket_price_index_key& l,
      const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;

#else
  friend TRADE_SDK_COMMON_API bool operator!=(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                              const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator<(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                             const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator<=(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                              const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator>(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                             const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator>=(const atfw::trade_api::trade_order_ticket_price_index_key& l,
                                              const atfw::trade_api::trade_order_ticket_price_index_key& r) noexcept;
#endif
};

struct ATFW_UTIL_SYMBOL_VISIBLE trade_order_ticket_recommend_score_index_key {
  int64_t recommend_score;
  int64_t price;
  int64_t order_ticket_id;
  int32_t rarity;
  atfw::trade::EnTradePriceMode price_mode;

  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key() { *this = get_invalid_instance(); }
  ATFW_UTIL_FORCEINLINE ~trade_order_ticket_recommend_score_index_key() {}

  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key(
      int64_t input_recommend_score, int64_t input_price, int64_t input_order_ticket_id, int32_t input_rarity,
      atfw::trade::EnTradePriceMode input_price_mode) noexcept
      : recommend_score(input_recommend_score),
        price(input_price),
        order_ticket_id(input_order_ticket_id),
        rarity(input_rarity),
        price_mode(input_price_mode) {}

  ATFW_UTIL_FORCEINLINE explicit trade_order_ticket_recommend_score_index_key(
      const atfw::trade::DTradeOrderTicketSearchBound& bound_exclude) noexcept
      : recommend_score(bound_exclude.recommend_score()),
        price(bound_exclude.price()),
        order_ticket_id(bound_exclude.trade_order_ticket_id()),
        rarity(bound_exclude.rarity()),
        price_mode(bound_exclude.price_mode()) {}

  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key(
      atfw::trade::EnTradePriceMode input_price_mode, const atfw::trade::DTradeOrderTicketMeta& meta) noexcept
      : recommend_score(meta.recommend_score()),
        price(get_order_ticket_price(input_price_mode, meta)),
        order_ticket_id(meta.trade_order_ticket_id()),
        rarity(meta.label_cache_rarity()),
        price_mode(input_price_mode) {}

  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key(
      const trade_order_ticket_recommend_score_index_key&) = default;
  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key(trade_order_ticket_recommend_score_index_key&&) =
      default;
  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key& operator=(
      const trade_order_ticket_recommend_score_index_key&) = default;
  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_score_index_key& operator=(
      trade_order_ticket_recommend_score_index_key&&) = default;

  TRADE_SDK_COMMON_API bool is_valid() const noexcept;

  struct hash_type {
    TRADE_SDK_COMMON_API size_t operator()(const trade_order_ticket_recommend_score_index_key&) const noexcept;
  };

  TRADE_SDK_COMMON_API static const trade_order_ticket_recommend_score_index_key& get_invalid_instance() noexcept;

  TRADE_SDK_COMMON_API static trade_order_ticket_recommend_score_index_key min(
      atfw::trade::EnTradePriceMode input_price_type) noexcept;
  TRADE_SDK_COMMON_API static trade_order_ticket_recommend_score_index_key max(
      atfw::trade::EnTradePriceMode input_price_type) noexcept;

  TRADE_SDK_COMMON_API static bool less(const trade_order_ticket_recommend_score_index_key& l,
                                        const trade_order_ticket_recommend_score_index_key& r) noexcept;
  TRADE_SDK_COMMON_API static bool equal(const trade_order_ticket_recommend_score_index_key& l,
                                         const trade_order_ticket_recommend_score_index_key& r) noexcept;

  friend TRADE_SDK_COMMON_API bool operator==(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;

#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
  friend TRADE_SDK_COMMON_API std::strong_ordering operator<=>(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;
#else
  friend TRADE_SDK_COMMON_API bool operator!=(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator<(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator<=(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator>(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator>=(
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& l,
      const atfw::trade_api::trade_order_ticket_recommend_score_index_key& r) noexcept;
#endif
};

struct ATFW_UTIL_SYMBOL_VISIBLE trade_order_ticket_recommend_single_price_value {
  int64_t min_price = 0;
  bool has_order = false;

  ATFW_UTIL_FORCEINLINE trade_order_ticket_recommend_single_price_value() noexcept {}

  inline explicit trade_order_ticket_recommend_single_price_value(int64_t input_min_price,
                                                                  bool input_has_order) noexcept
      : min_price(input_min_price), has_order(input_has_order) {}

  TRADE_SDK_COMMON_API bool is_valid() const;

  TRADE_SDK_COMMON_API static bool need_update(const trade_order_ticket_recommend_single_price_value& previous,
                                               const trade_order_ticket_recommend_single_price_value& current);

  TRADE_SDK_COMMON_API static trade_order_ticket_recommend_single_price_value min() noexcept;
  TRADE_SDK_COMMON_API static trade_order_ticket_recommend_single_price_value max() noexcept;

  TRADE_SDK_COMMON_API static bool less(const trade_order_ticket_recommend_single_price_value& l,
                                        const trade_order_ticket_recommend_single_price_value& r) noexcept;
  TRADE_SDK_COMMON_API static bool equal(const trade_order_ticket_recommend_single_price_value& l,
                                         const trade_order_ticket_recommend_single_price_value& r) noexcept;

  friend TRADE_SDK_COMMON_API bool operator==(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;

#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
  friend TRADE_SDK_COMMON_API std::strong_ordering operator<=>(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;
#else
  friend TRADE_SDK_COMMON_API bool operator!=(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator<(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator<=(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator>(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;
  friend TRADE_SDK_COMMON_API bool operator>=(
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& l,
      const atfw::trade_api::trade_order_ticket_recommend_single_price_value& r) noexcept;
#endif
};

}  // namespace trade_api
}  // namespace atframework

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_BEGIN
template <class CharT>
struct formatter<atfw::trade_api::trade_order_ticket_price_index_key, CharT> : formatter<const char*, CharT> {
  template <class FormatContext>
  auto format(const atfw::trade_api::trade_order_ticket_price_index_key& key, FormatContext& ctx) const {
    return ATFRAMEWORK_UTILS_STRING_FWAPI_FORMAT_TO(ctx.out(), "order_ticket_price_key ({},{},{})",
                                                    static_cast<int32_t>(key.price_type), key.price,
                                                    key.order_ticket_id);
  }
};

template <class CharT>
struct formatter<atfw::trade_api::trade_order_ticket_recommend_score_index_key, CharT> : formatter<const char*, CharT> {
  template <class FormatContext>
  auto format(const atfw::trade_api::trade_order_ticket_recommend_score_index_key& key, FormatContext& ctx) const {
    return ATFRAMEWORK_UTILS_STRING_FWAPI_FORMAT_TO(ctx.out(), "order_ticket_recommend_score_index_key ({},{},{})",
                                                    key.recommend_score, key.price, key.order_ticket_id);
  }
};

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_END
