// Copyright 2026 atframework
// Created by owent on 2026-11-26.
//

#pragma once

#include <config/compile_optimize.h>

#include <nostd/function_ref.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.trade.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>
#include <protocol/pbdesc/svr.struct.trade.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <transaction_client_handle.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "trade/trade_index_ulility.h"

namespace atframework {
namespace trade_api {

struct ATFW_UTIL_SYMBOL_VISIBLE product_key_hash_type {
  TRADE_SDK_COMMON_API size_t operator()(int32_t area_code, int32_t trade_product_id, int32_t ext_id,
                                         uint64_t seed) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeMarketProductKey &key) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeMarketProductKey &key, size_t index) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeMarketProductKey &key, size_t index,
                                         uint64_t seed) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeMarketProductKey &key, int32_t id,
                                         uint64_t seed) const noexcept;
};

struct ATFW_UTIL_SYMBOL_VISIBLE product_key_equal_type {
  TRADE_SDK_COMMON_API bool operator()(const atfw::trade::DTradeMarketProductKey &left,
                                       const atfw::trade::DTradeMarketProductKey &right) const noexcept;
};

struct ATFW_UTIL_SYMBOL_VISIBLE standard_price_key_hash_type {
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeStandardPriceKey &key) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeStandardPriceKey &key, size_t index) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeStandardPriceKey &key, size_t index,
                                         uint64_t seed) const noexcept;
  TRADE_SDK_COMMON_API size_t operator()(const atfw::trade::DTradeStandardPriceKey &key, int32_t id,
                                         uint64_t seed) const noexcept;
};

struct ATFW_UTIL_SYMBOL_VISIBLE standard_price_key_equal_type {
  TRADE_SDK_COMMON_API bool operator()(const atfw::trade::DTradeStandardPriceKey &lhs,
                                       const atfw::trade::DTradeStandardPriceKey &rhs) const noexcept;
};

TRADE_SDK_COMMON_API atfw::trade::trade_order_ticket_key trade_ticket_meta_to_key(
    const atfw::trade::DTradeOrderTicketMeta &meta);

struct ATFW_UTIL_SYMBOL_VISIBLE trade_pair_hash_type {
  TRADE_SDK_COMMON_API size_t operator()(const std::pair<int32_t, int32_t> &key) const noexcept;
};

struct ATFW_UTIL_SYMBOL_VISIBLE trade_pair_equal_type {
  TRADE_SDK_COMMON_API bool operator()(const std::pair<int32_t, int32_t> &left,
                                       const std::pair<int32_t, int32_t> &right) const noexcept;
};

TRADE_SDK_COMMON_API std::string trade_ticket_to_transaction_participator_key(int32_t area_code,
                                                                              int64_t order_ticket_id, int32_t slot_id,
                                                                              int32_t order_type);
TRADE_SDK_COMMON_API atfw::trade::trade_order_ticket_key transaction_participator_key_to_trade_ticket(
    const std::string &key);

TRADE_SDK_COMMON_API const ::atfw::distributed_system::transaction_client_handle::transaction_options &
get_normal_transaction_options();

TRADE_SDK_COMMON_API const ::atfw::distributed_system::transaction_client_handle::transaction_options &
get_force_commit_transaction_options();

TRADE_SDK_COMMON_API void normalize(atfw::trade::DTradeOrderTicketLabel &label);

TRADE_SDK_COMMON_API void normalize(atfw::trade::DTradeOrderTicketSearchRule &rule);

TRADE_SDK_COMMON_API bool product_key_is_valid(const atfw::trade::DTradeMarketProductKey &key);

TRADE_SDK_COMMON_API bool price_key_is_valid(const atfw::trade::DTradeStandardPriceKey &key);

class product_label_view_key {
 public:
  struct ATFW_UTIL_SYMBOL_VISIBLE hash_type {
    TRADE_SDK_COMMON_API size_t operator()(const product_label_view_key &key) const noexcept;
  };

  struct ATFW_UTIL_SYMBOL_VISIBLE equal_type {
    TRADE_SDK_COMMON_API bool operator()(const product_label_view_key &l,
                                         const product_label_view_key &r) const noexcept;
  };

  struct ATFW_UTIL_SYMBOL_VISIBLE hash_equal_no_bound_exclude {
    TRADE_SDK_COMMON_API bool operator()(const product_label_view_key &l,
                                         const product_label_view_key &r) const noexcept;
  };

  struct ATFW_UTIL_SYMBOL_VISIBLE ctor_reset_options {
    int32_t trade_market_id = 0;
    int32_t major_index_type = 0;
    int32_t minor_index_type = 0;

    ATFW_UTIL_FORCEINLINE ctor_reset_options() {}
  };

 private:
  TRADE_SDK_COMMON_API explicit product_label_view_key(
      std::shared_ptr<atfw::trade::DTradeOrderTicketSearchRule> &&rule);

 public:
  product_label_view_key() = delete;

  TRADE_SDK_COMMON_API explicit product_label_view_key(const atfw::trade::DTradeOrderTicketSearchRule &origin);
  TRADE_SDK_COMMON_API product_label_view_key(const ctor_reset_options &reset_options,
                                              const atfw::trade::DTradeOrderTicketSearchRule &origin);
  TRADE_SDK_COMMON_API product_label_view_key(int32_t trade_market_id,
                                              const atfw::trade::DTradeProductSearchRule &origin);
  TRADE_SDK_COMMON_API ~product_label_view_key();

  TRADE_SDK_COMMON_API product_label_view_key(product_label_view_key &&other) noexcept;
  TRADE_SDK_COMMON_API product_label_view_key(const product_label_view_key &other);

  TRADE_SDK_COMMON_API product_label_view_key &operator=(const product_label_view_key &other) noexcept;
  TRADE_SDK_COMMON_API product_label_view_key &operator=(product_label_view_key &&other) noexcept;

  TRADE_SDK_COMMON_API const atfw::trade::DTradeOrderTicketSearchRule &get_search_rule() const noexcept;

  TRADE_SDK_COMMON_API bool check_subview(const product_label_view_key &checked_subview,
                                          bool ignore_bound_exclude = false) const noexcept;

  TRADE_SDK_COMMON_API size_t hash_code() const noexcept;
  TRADE_SDK_COMMON_API size_t hash_code_ignore_bound_value() const noexcept;

  TRADE_SDK_COMMON_API bool is_page_start() const noexcept;

  TRADE_SDK_COMMON_API product_label_view_key make_page_start() const noexcept;

  TRADE_SDK_COMMON_API bool match(const atfw::trade::DTradeOrderTicketLabel &label) const noexcept;

  TRADE_SDK_COMMON_API bool operator==(const product_label_view_key &other) const noexcept;

  TRADE_SDK_COMMON_API static bool equal(const product_label_view_key &l, const product_label_view_key &r,
                                         bool ignore_bound_exclude = false, bool ignore_bound_value = false) noexcept;

  TRADE_SDK_COMMON_API static bool equal(const atfw::trade::DTradeOrderTicketSearchRule &l,
                                         const atfw::trade::DTradeOrderTicketSearchRule &r,
                                         bool ignore_bound_exclude = false, bool ignore_bound_value = false) noexcept;

  TRADE_SDK_COMMON_API static bool equal(const atfw::trade::DTradeOrderTicketLabel &l,
                                         const atfw::trade::DTradeOrderTicketLabel &r) noexcept;

  TRADE_SDK_COMMON_API bool has_bound_exclude() const noexcept;

 private:
  size_t hash_code_;
  size_t hash_code_ignore_bound_value_;
  std::shared_ptr<atfw::trade::DTradeOrderTicketSearchRule> rule_;
};

class area_product_view_key {
 public:
  struct ATFW_UTIL_SYMBOL_VISIBLE hash_type {
    TRADE_SDK_COMMON_API size_t operator()(const area_product_view_key &key) const noexcept;
  };
  struct ATFW_UTIL_SYMBOL_VISIBLE equal_type {
    TRADE_SDK_COMMON_API bool operator()(const area_product_view_key &l, const area_product_view_key &r) const noexcept;
  };

  struct ATFW_UTIL_SYMBOL_VISIBLE ignore_bound_hash_type {
    TRADE_SDK_COMMON_API size_t operator()(const area_product_view_key &key) const noexcept;
  };
  struct ATFW_UTIL_SYMBOL_VISIBLE ignore_bound_equal_type {
    TRADE_SDK_COMMON_API bool operator()(const area_product_view_key &l, const area_product_view_key &r) const noexcept;
  };

  area_product_view_key() = delete;

 public:
  TRADE_SDK_COMMON_API area_product_view_key(int32_t area_code, atfw::trade::DTradeOrderTicketMeta::TypeCase order_type,
                                             const atfw::trade::DTradeOrderTicketSearchRule &origin);

  TRADE_SDK_COMMON_API area_product_view_key(int32_t area_code, atfw::trade::DTradeOrderTicketMeta::TypeCase order_type,
                                             product_label_view_key &&label_view);

  TRADE_SDK_COMMON_API ~area_product_view_key();

  TRADE_SDK_COMMON_API area_product_view_key(area_product_view_key &&other) noexcept;
  TRADE_SDK_COMMON_API area_product_view_key(const area_product_view_key &other);

  TRADE_SDK_COMMON_API area_product_view_key &operator=(const area_product_view_key &other) noexcept;
  TRADE_SDK_COMMON_API area_product_view_key &operator=(area_product_view_key &&other) noexcept;

  TRADE_SDK_COMMON_API size_t hash_code() const noexcept;
  TRADE_SDK_COMMON_API size_t hash_code_ignore_bound_value() const noexcept;

  TRADE_SDK_COMMON_API int32_t get_area_code() const noexcept;

  TRADE_SDK_COMMON_API atfw::trade::DTradeOrderTicketMeta::TypeCase get_order_type() const noexcept;

  TRADE_SDK_COMMON_API const product_label_view_key &get_label_view() const noexcept;

  TRADE_SDK_COMMON_API bool is_page_start() const noexcept;

  TRADE_SDK_COMMON_API area_product_view_key make_page_start() const noexcept;

  TRADE_SDK_COMMON_API trade_order_ticket_price_index_key
  make_price_bound_key(atfw::trade::EnTradePriceMode price_mode) const noexcept;

  TRADE_SDK_COMMON_API trade_order_ticket_recommend_score_index_key
  make_recommend_score_bound_key(atfw::trade::EnTradePriceMode price_mode) const noexcept;

  TRADE_SDK_COMMON_API bool operator==(const area_product_view_key &other) const noexcept;

  TRADE_SDK_COMMON_API const atfw::trade::DTradeOrderTicketSearchBound &get_bound_exclude() const noexcept;

  TRADE_SDK_COMMON_API inline bool has_bound_exclude() const noexcept { return label_view_.has_bound_exclude(); }

 private:
  int32_t area_code_;
  atfw::trade::DTradeOrderTicketMeta::TypeCase order_type_;
  product_label_view_key label_view_;
  size_t hash_code_;
  size_t hash_code_ignore_bound_value_;
};

}  // namespace trade_api
}  // namespace atframework
