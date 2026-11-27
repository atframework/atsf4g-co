// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#pragma once

#include <time/jiffies_timer.h>

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include <trade/trade_index_ulility.h>

namespace atframework {
namespace trade {
class DTradeMarketProductBasicIndexInfo;
class DTradeIndexStandardPriceResult;
class DTradeIndexSearchStandardPriceRule;
}  // namespace trade
}  // namespace atframework

namespace atframework {
namespace trade_api {

// 热销推荐排序Key: (有上架->销量->商品ID)
struct product_index_hot_sale_key {
  bool has_order = false;
  int64_t deal_count = 0;
  int64_t lowest_price = 0;
  int32_t product_id = 0;

  inline product_index_hot_sale_key() noexcept {}

  TRADE_SDK_INDEX_API static product_index_hot_sale_key min() noexcept;
  TRADE_SDK_INDEX_API static product_index_hot_sale_key max() noexcept;
};

TRADE_SDK_INDEX_API bool operator==(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept;
inline bool operator!=(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept {
  return !(l == r);
}

TRADE_SDK_INDEX_API bool operator<(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept;
inline bool operator<=(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept {
  return !(r < l);
}
inline bool operator>(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept {
  return r < l;
}
inline bool operator>=(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept {
  return !(l < r);
}

// 位置推荐排序Key: (装备位置->(有上架->稀有度(高到低)->推荐分->商品ID))
// 这个Key比较特别，第一层采用位置推荐隔离，第二层从稀有度索引查找。查找流程为先查找有上架。
struct product_index_wear_part_key {
  bool has_order = false;
  int32_t rarity = 0;
  int64_t search_order = 0;
  int64_t recommend_score = 0;
  int64_t lowest_price = 0;
  int32_t product_id = 0;

  inline product_index_wear_part_key() noexcept {}

  TRADE_SDK_INDEX_API static product_index_wear_part_key min() noexcept;
  TRADE_SDK_INDEX_API static product_index_wear_part_key max() noexcept;
};

TRADE_SDK_INDEX_API bool operator==(const product_index_wear_part_key& l,
                                    const product_index_wear_part_key& r) noexcept;
inline bool operator!=(const product_index_wear_part_key& l, const product_index_wear_part_key& r) noexcept {
  return !(l == r);
}

TRADE_SDK_INDEX_API bool operator<(const product_index_wear_part_key& l, const product_index_wear_part_key& r) noexcept;
inline bool operator<=(const product_index_wear_part_key& l, const product_index_wear_part_key& r) noexcept {
  return !(r < l);
}
inline bool operator>(const product_index_wear_part_key& l, const product_index_wear_part_key& r) noexcept {
  return r < l;
}
inline bool operator>=(const product_index_wear_part_key& l, const product_index_wear_part_key& r) noexcept {
  return !(l < r);
}

struct product_index_wear_part_store_key {
  std::vector<int32_t> wear_parts;
  product_index_wear_part_key index;

  inline product_index_wear_part_store_key() noexcept {}
};

TRADE_SDK_INDEX_API bool operator==(const product_index_wear_part_store_key& l,
                                    const product_index_wear_part_store_key& r) noexcept;
inline bool operator!=(const product_index_wear_part_store_key& l,
                       const product_index_wear_part_store_key& r) noexcept {
  return !(l == r);
}

// 装备类型推荐排序Key: (装备类型->(有上架->稀有度(高到低)->推荐分->商品ID))
// 这个Key比较特别，第一层采用位置推荐隔离，第二层从稀有度索引查找。查找流程为先查找有上架。
struct product_index_gear_sub_type_key {
  bool has_order = false;
  int32_t rarity = 0;
  int64_t search_order = 0;
  int64_t recommend_score = 0;
  int64_t lowest_price = 0;
  int32_t product_id = 0;

  inline product_index_gear_sub_type_key() noexcept {}

  TRADE_SDK_INDEX_API static product_index_gear_sub_type_key min() noexcept;
  TRADE_SDK_INDEX_API static product_index_gear_sub_type_key max() noexcept;
};

TRADE_SDK_INDEX_API bool operator==(const product_index_gear_sub_type_key& l,
                                    const product_index_gear_sub_type_key& r) noexcept;
inline bool operator!=(const product_index_gear_sub_type_key& l, const product_index_gear_sub_type_key& r) noexcept {
  return !(l == r);
}

TRADE_SDK_INDEX_API bool operator<(const product_index_gear_sub_type_key& l,
                                   const product_index_gear_sub_type_key& r) noexcept;
inline bool operator<=(const product_index_gear_sub_type_key& l, const product_index_gear_sub_type_key& r) noexcept {
  return !(r < l);
}
inline bool operator>(const product_index_gear_sub_type_key& l, const product_index_gear_sub_type_key& r) noexcept {
  return r < l;
}
inline bool operator>=(const product_index_gear_sub_type_key& l, const product_index_gear_sub_type_key& r) noexcept {
  return !(l < r);
}

struct product_index_gear_sub_type_store_key {
  std::vector<int32_t> gear_sub_types;
  product_index_gear_sub_type_key index;

  inline product_index_gear_sub_type_store_key() noexcept {}
};

TRADE_SDK_INDEX_API bool operator==(const product_index_gear_sub_type_store_key& l,
                                    const product_index_gear_sub_type_store_key& r) noexcept;
inline bool operator!=(const product_index_gear_sub_type_store_key& l,
                       const product_index_gear_sub_type_store_key& r) noexcept {
  return !(l == r);
}

// 搜索排序Key: (有上架->稀有度(低到高)->类型(武器>饰品>防具)->价格(低到高)->商品ID)
struct product_index_search_key {
  bool has_order = false;
  int32_t rarity = 0;
  int64_t search_order = 0;
  int64_t recommend_score = 0;
  int64_t lowest_price = 0;
  int32_t product_id = 0;

  inline product_index_search_key() noexcept {}

  TRADE_SDK_INDEX_API static product_index_search_key min() noexcept;
  TRADE_SDK_INDEX_API static product_index_search_key max() noexcept;
};

TRADE_SDK_INDEX_API bool operator==(const product_index_search_key& l, const product_index_search_key& r) noexcept;
inline bool operator!=(const product_index_search_key& l, const product_index_search_key& r) noexcept {
  return !(l == r);
}

TRADE_SDK_INDEX_API bool operator<(const product_index_search_key& l, const product_index_search_key& r) noexcept;
inline bool operator<=(const product_index_search_key& l, const product_index_search_key& r) noexcept {
  return !(r < l);
}
inline bool operator>(const product_index_search_key& l, const product_index_search_key& r) noexcept { return r < l; }
inline bool operator>=(const product_index_search_key& l, const product_index_search_key& r) noexcept {
  return !(l < r);
}

struct product_index_single_price_key {
  std::unordered_set<int32_t> careers;
  int32_t rarity = 0;
  std::unordered_set<int32_t> affix_gear_sets;
  std::unordered_set<int32_t> gear_sub_types;
  // int64_t min_price = 0;
  atfw::trade_api::trade_order_ticket_recommend_single_price_value min_price;

  inline product_index_single_price_key() noexcept {}
};

TRADE_SDK_INDEX_API bool operator==(const product_index_single_price_key& l,
                                    const product_index_single_price_key& r) noexcept;
inline bool operator!=(const product_index_single_price_key& l, const product_index_single_price_key& r) noexcept {
  return !(l == r);
}
TRADE_SDK_INDEX_API bool check_career_rarity_affix_gear_euqal(const product_index_single_price_key& l,
                                                              const product_index_single_price_key& r) noexcept;

struct product_affix_gearsubtype_key {
  int32_t affix_gear = 0;
  int32_t gear_subtype = 0;
  inline product_affix_gearsubtype_key() noexcept {}
  inline product_affix_gearsubtype_key(int32_t affix, int32_t gear) noexcept : affix_gear(affix), gear_subtype(gear) {}
};

TRADE_SDK_INDEX_API bool operator==(const product_affix_gearsubtype_key& l,
                                    const product_affix_gearsubtype_key& r) noexcept;
inline bool operator!=(const product_affix_gearsubtype_key& l, const product_affix_gearsubtype_key& r) noexcept {
  return !(l == r);
}
struct ATFW_UTIL_SYMBOL_VISIBLE product_affix_gearsubtype_key_hash_type {
  TRADE_SDK_INDEX_API size_t operator()(const product_affix_gearsubtype_key& key) const noexcept;
};

// 基准价格排序Key: (有上架->销量->商品ID)
struct standard_price_index_key {
  int32_t order_type = 0;
  bool price_ascending = false;
  int64_t standard_price = 0;
  int64_t lowest_price = 0;
  int64_t timestamp_second = 0;
  int64_t summary_version = 0;
  int32_t product_id = 0;

  inline standard_price_index_key() noexcept {}

  TRADE_SDK_INDEX_API standard_price_index_key(
      const atframework::trade::DTradeIndexSearchStandardPriceRule& rule,
      const atframework::trade::DTradeMarketProductBasicIndexInfo& basic_info) noexcept;

  TRADE_SDK_INDEX_API standard_price_index_key(
      const atframework::trade::DTradeIndexStandardPriceResult& result,
      const atframework::trade::DTradeMarketProductBasicIndexInfo& basic_info) noexcept;

  TRADE_SDK_INDEX_API static standard_price_index_key min(
      const atframework::trade::DTradeIndexSearchStandardPriceRule& rule) noexcept;
  TRADE_SDK_INDEX_API static standard_price_index_key min(
      const atframework::trade::DTradeIndexStandardPriceResult& result) noexcept;
  TRADE_SDK_INDEX_API static standard_price_index_key max(
      const atframework::trade::DTradeIndexSearchStandardPriceRule& rule) noexcept;
  TRADE_SDK_INDEX_API static standard_price_index_key max(
      const atframework::trade::DTradeIndexStandardPriceResult& result) noexcept;
};

TRADE_SDK_INDEX_API bool operator==(const standard_price_index_key& l, const standard_price_index_key& r) noexcept;
inline bool operator!=(const standard_price_index_key& l, const standard_price_index_key& r) noexcept {
  return !(l == r);
}

TRADE_SDK_INDEX_API bool operator<(const standard_price_index_key& l, const standard_price_index_key& r) noexcept;
inline bool operator<=(const standard_price_index_key& l, const standard_price_index_key& r) noexcept {
  return !(r < l);
}
inline bool operator>(const standard_price_index_key& l, const standard_price_index_key& r) noexcept { return r < l; }
inline bool operator>=(const standard_price_index_key& l, const standard_price_index_key& r) noexcept {
  return !(l < r);
}

}  // namespace trade_api
}  // namespace atframework
