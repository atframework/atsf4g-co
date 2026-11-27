// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#include "rpc/trade/index_algorithm.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.trade.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cassert>
#include <limits>

#ifdef min
#  undef min
#endif

#ifdef max
#  undef max
#endif

namespace atframework {
namespace trade_api {

TRADE_SDK_INDEX_API product_index_hot_sale_key product_index_hot_sale_key::min() noexcept {
  product_index_hot_sale_key ret;
  ret.has_order = true;
  ret.deal_count = std::numeric_limits<decltype(ret.deal_count)>::max();
  ret.lowest_price = std::numeric_limits<decltype(ret.lowest_price)>::min();
  ret.product_id = std::numeric_limits<decltype(ret.product_id)>::min();

  assert(ret < product_index_hot_sale_key{});

  return ret;
}

TRADE_SDK_INDEX_API product_index_hot_sale_key product_index_hot_sale_key::max() noexcept {
  product_index_hot_sale_key ret;
  ret.has_order = false;
  ret.deal_count = std::numeric_limits<decltype(ret.deal_count)>::min();
  ret.lowest_price = std::numeric_limits<decltype(ret.lowest_price)>::max();
  ret.product_id = std::numeric_limits<decltype(ret.product_id)>::max();

  assert(product_index_hot_sale_key{} < ret);

  return ret;
}

TRADE_SDK_INDEX_API bool operator==(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept {
  return l.has_order == r.has_order && l.deal_count == r.deal_count && l.lowest_price == r.lowest_price &&
         l.product_id == r.product_id;
}

TRADE_SDK_INDEX_API bool operator<(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r) noexcept {
  // 注意以下几个位置算法保持一致
  //   global_search_cache_manager.cpp 中 sort_query_product_hot_sale
  //   index_algorithm.cpp(sdk)中operator<(const product_index_hot_sale_key& l, const product_index_hot_sale_key& r)

  if (l.has_order != r.has_order) {
    return l.has_order;
  }

  if (l.deal_count != r.deal_count) {
    return l.deal_count > r.deal_count;
  }

  if (l.lowest_price != r.lowest_price) {
    return l.lowest_price < r.lowest_price;
  }

  return l.product_id < r.product_id;
}

TRADE_SDK_INDEX_API product_index_wear_part_key product_index_wear_part_key::min() noexcept {
  product_index_wear_part_key ret;
  ret.has_order = true;
  ret.rarity = std::numeric_limits<decltype(ret.rarity)>::max();
  ret.search_order = std::numeric_limits<decltype(ret.search_order)>::min();
  ret.recommend_score = std::numeric_limits<decltype(ret.recommend_score)>::min();
  ret.lowest_price = std::numeric_limits<decltype(ret.lowest_price)>::min();
  ret.product_id = std::numeric_limits<decltype(ret.product_id)>::min();

  assert(ret < product_index_wear_part_key{});

  return ret;
}

TRADE_SDK_INDEX_API product_index_wear_part_key product_index_wear_part_key::max() noexcept {
  product_index_wear_part_key ret;
  ret.has_order = false;
  ret.rarity = std::numeric_limits<decltype(ret.rarity)>::min();
  ret.search_order = std::numeric_limits<decltype(ret.search_order)>::max();
  ret.recommend_score = std::numeric_limits<decltype(ret.recommend_score)>::max();
  ret.lowest_price = std::numeric_limits<decltype(ret.lowest_price)>::max();
  ret.product_id = std::numeric_limits<decltype(ret.product_id)>::max();

  assert(product_index_wear_part_key{} < ret);

  return ret;
}

TRADE_SDK_INDEX_API bool operator==(const product_index_wear_part_key& l,
                                    const product_index_wear_part_key& r) noexcept {
  return l.has_order == r.has_order && l.rarity == r.rarity && l.search_order == r.search_order &&
         l.recommend_score == r.recommend_score && l.lowest_price == r.lowest_price && l.product_id == r.product_id;
}

TRADE_SDK_INDEX_API bool operator<(const product_index_wear_part_key& l,
                                   const product_index_wear_part_key& r) noexcept {
  if (l.has_order != r.has_order) {
    return l.has_order;
  }

  // 注意以下几个位置算法保持一致
  //   global_search_cache_manager.cpp 中 sort_query_product_wear_part
  //   index_algorithm.cpp(sdk)中operator<(const product_index_wear_part_key& l, const product_index_wear_part_key& r)
  if (l.rarity != r.rarity) {
    return l.rarity > r.rarity;
  }

  if (l.search_order != r.search_order) {
    return l.search_order < r.search_order;
  }

  if (l.recommend_score != r.recommend_score) {
    return l.recommend_score < r.recommend_score;
  }

  if (l.lowest_price != r.lowest_price) {
    return l.lowest_price < r.lowest_price;
  }

  return l.product_id < r.product_id;
}

TRADE_SDK_INDEX_API bool operator==(const product_index_wear_part_store_key& l,
                                    const product_index_wear_part_store_key& r) noexcept {
  if (l.index != r.index || l.wear_parts.size() != r.wear_parts.size()) {
    return false;
  }

  auto iter_l = l.wear_parts.begin();
  auto iter_r = r.wear_parts.begin();
  while (iter_l != l.wear_parts.end() && iter_r != r.wear_parts.end()) {
    if (*iter_l != *iter_r) {
      return false;
    }

    ++iter_l;
    ++iter_r;
  }
  return true;
}

TRADE_SDK_INDEX_API product_index_search_key product_index_search_key::min() noexcept {
  product_index_search_key ret;
  ret.has_order = true;
  ret.rarity = std::numeric_limits<decltype(ret.rarity)>::min();
  ret.search_order = std::numeric_limits<decltype(ret.search_order)>::min();
  ret.recommend_score = std::numeric_limits<decltype(ret.recommend_score)>::min();
  ret.lowest_price = std::numeric_limits<decltype(ret.lowest_price)>::min();
  ret.product_id = std::numeric_limits<decltype(ret.product_id)>::min();

  assert(ret < product_index_search_key{});

  return ret;
}

TRADE_SDK_INDEX_API product_index_search_key product_index_search_key::max() noexcept {
  product_index_search_key ret;
  ret.has_order = false;
  ret.rarity = std::numeric_limits<decltype(ret.rarity)>::max();
  ret.search_order = std::numeric_limits<decltype(ret.search_order)>::max();
  ret.recommend_score = std::numeric_limits<decltype(ret.recommend_score)>::max();
  ret.lowest_price = std::numeric_limits<decltype(ret.lowest_price)>::max();
  ret.product_id = std::numeric_limits<decltype(ret.product_id)>::max();

  assert(product_index_search_key{} < ret);

  return ret;
}

TRADE_SDK_INDEX_API bool operator==(const product_index_search_key& l, const product_index_search_key& r) noexcept {
  return l.has_order == r.has_order && l.rarity == r.rarity && l.search_order == r.search_order &&
         l.recommend_score == r.recommend_score && l.lowest_price == r.lowest_price && l.product_id == r.product_id;
}

TRADE_SDK_INDEX_API bool operator<(const product_index_search_key& l, const product_index_search_key& r) noexcept {
  // 注意以下几个位置算法保持一致
  //   global_search_cache_manager.cpp 中 sort_query_product_custom_search
  //   index_algorithm.cpp(sdk) 中 operator<(const product_index_search_key& l, const product_index_search_key& r)
  if (l.has_order != r.has_order) {
    return l.has_order;
  }

  if (l.rarity != r.rarity) {
    return l.rarity < r.rarity;
  }

  if (l.search_order != r.search_order) {
    return l.search_order < r.search_order;
  }

  if (l.recommend_score != r.recommend_score) {
    return l.recommend_score < r.recommend_score;
  }

  if (l.lowest_price != r.lowest_price) {
    return l.lowest_price < r.lowest_price;
  }

  return l.product_id < r.product_id;
}

TRADE_SDK_INDEX_API bool operator==(const product_index_single_price_key& l,
                                    const product_index_single_price_key& r) noexcept {
  if (l.min_price != r.min_price) {
    return false;
  }
  return true;
}

TRADE_SDK_INDEX_API standard_price_index_key::standard_price_index_key(
    const atfw::trade::DTradeIndexSearchStandardPriceRule& rule,
    const atfw::trade::DTradeMarketProductBasicIndexInfo& basic_info) noexcept
    : order_type(rule.order_type()),
      price_ascending(rule.sort_by() == atfw::trade::EN_TRADE_STANDARD_PRICE_SORT_ASCENDING),
      standard_price(basic_info.standard_price()),
      lowest_price(basic_info.lowest_price()),
      timestamp_second(basic_info.timestamp().seconds()),
      summary_version(basic_info.summary_version()),
      product_id(basic_info.trade_product_key().trade_product_id()) {}

TRADE_SDK_INDEX_API standard_price_index_key::standard_price_index_key(
    const atfw::trade::DTradeIndexStandardPriceResult& result,
    const atfw::trade::DTradeMarketProductBasicIndexInfo& basic_info) noexcept
    : order_type(result.order_type()),
      price_ascending(result.sort_by() == atfw::trade::EN_TRADE_STANDARD_PRICE_SORT_ASCENDING),
      standard_price(basic_info.standard_price()),
      lowest_price(basic_info.lowest_price()),
      timestamp_second(basic_info.timestamp().seconds()),
      summary_version(basic_info.summary_version()),
      product_id(basic_info.trade_product_key().trade_product_id()) {}

TRADE_SDK_INDEX_API standard_price_index_key
standard_price_index_key::min(const atfw::trade::DTradeIndexSearchStandardPriceRule& rule) noexcept {
  standard_price_index_key ret;
  ret.order_type = rule.order_type();
  ret.price_ascending = rule.sort_by() == atfw::trade::EN_TRADE_STANDARD_PRICE_SORT_ASCENDING;
  if (ret.price_ascending) {
    ret.standard_price = std::numeric_limits<int64_t>::min();
    ret.lowest_price = std::numeric_limits<int64_t>::min();
    ret.timestamp_second = 0;
    ret.summary_version = 0;
    ret.product_id = 0;
  } else {
    ret.standard_price = std::numeric_limits<int64_t>::max();
    ret.lowest_price = std::numeric_limits<int64_t>::max();
    ret.timestamp_second = 0;
    ret.summary_version = 0;
    ret.product_id = 0;
  }

  return ret;
}

TRADE_SDK_INDEX_API standard_price_index_key
standard_price_index_key::min(const atfw::trade::DTradeIndexStandardPriceResult& result) noexcept {
  standard_price_index_key ret;
  ret.order_type = result.order_type();
  ret.price_ascending = result.sort_by() == atfw::trade::EN_TRADE_STANDARD_PRICE_SORT_ASCENDING;
  if (ret.price_ascending) {
    ret.standard_price = std::numeric_limits<int64_t>::min();
    ret.lowest_price = std::numeric_limits<int64_t>::min();
    ret.timestamp_second = 0;
    ret.summary_version = 0;
    ret.product_id = 0;
  } else {
    ret.standard_price = std::numeric_limits<int64_t>::max();
    ret.lowest_price = std::numeric_limits<int64_t>::max();
    ret.timestamp_second = 0;
    ret.summary_version = 0;
    ret.product_id = 0;
  }

  return ret;
}

TRADE_SDK_INDEX_API standard_price_index_key
standard_price_index_key::max(const atfw::trade::DTradeIndexSearchStandardPriceRule& rule) noexcept {
  standard_price_index_key ret;
  ret.order_type = rule.order_type();
  ret.price_ascending = rule.sort_by() == atfw::trade::EN_TRADE_STANDARD_PRICE_SORT_ASCENDING;
  if (!ret.price_ascending) {
    ret.standard_price = std::numeric_limits<int64_t>::min();
    ret.lowest_price = std::numeric_limits<int64_t>::min();
    ret.timestamp_second = std::numeric_limits<int64_t>::max();
    ret.summary_version = std::numeric_limits<int64_t>::max();
    ret.product_id = std::numeric_limits<int32_t>::max();
  } else {
    ret.standard_price = std::numeric_limits<int64_t>::max();
    ret.lowest_price = std::numeric_limits<int64_t>::max();
    ret.timestamp_second = std::numeric_limits<int64_t>::max();
    ret.summary_version = std::numeric_limits<int64_t>::max();
    ret.product_id = std::numeric_limits<int32_t>::max();
  }

  return ret;
}

TRADE_SDK_INDEX_API standard_price_index_key
standard_price_index_key::max(const atfw::trade::DTradeIndexStandardPriceResult& result) noexcept {
  standard_price_index_key ret;
  ret.order_type = result.order_type();
  ret.price_ascending = result.sort_by() == atfw::trade::EN_TRADE_STANDARD_PRICE_SORT_ASCENDING;
  if (!ret.price_ascending) {
    ret.standard_price = std::numeric_limits<int64_t>::min();
    ret.lowest_price = std::numeric_limits<int64_t>::min();
    ret.timestamp_second = std::numeric_limits<int64_t>::max();
    ret.summary_version = std::numeric_limits<int64_t>::max();
    ret.product_id = std::numeric_limits<int32_t>::max();
  } else {
    ret.standard_price = std::numeric_limits<int64_t>::max();
    ret.lowest_price = std::numeric_limits<int64_t>::max();
    ret.timestamp_second = std::numeric_limits<int64_t>::max();
    ret.summary_version = std::numeric_limits<int64_t>::max();
    ret.product_id = std::numeric_limits<int32_t>::max();
  }

  return ret;
}

TRADE_SDK_INDEX_API bool operator==(const standard_price_index_key& l, const standard_price_index_key& r) noexcept {
  return l.order_type == r.order_type && l.price_ascending == r.price_ascending &&
         l.standard_price == r.standard_price && l.lowest_price == r.lowest_price &&
         l.timestamp_second == r.timestamp_second && l.summary_version == r.summary_version &&
         l.product_id == r.product_id;
}

TRADE_SDK_INDEX_API bool operator<(const standard_price_index_key& l, const standard_price_index_key& r) noexcept {
  if (l.order_type != r.order_type) {
    return l.order_type < r.order_type;
  }

  if (l.price_ascending != r.price_ascending) {
    return l.price_ascending;
  }

  if (l.price_ascending) {
    if (l.standard_price != r.standard_price) {
      return l.standard_price < r.standard_price;
    }
    if (l.lowest_price != r.lowest_price) {
      return l.lowest_price < r.lowest_price;
    }
    if (l.timestamp_second != r.timestamp_second) {
      return l.timestamp_second > r.timestamp_second;
    }
    if (l.summary_version != r.summary_version) {
      return l.summary_version > r.summary_version;
    }
    return l.product_id < r.product_id;
  }

  if (l.standard_price != r.standard_price) {
    return l.standard_price > r.standard_price;
  }
  if (l.lowest_price != r.lowest_price) {
    return l.lowest_price > r.lowest_price;
  }
  if (l.timestamp_second != r.timestamp_second) {
    return l.timestamp_second > r.timestamp_second;
  }
  if (l.summary_version != r.summary_version) {
    return l.summary_version > r.summary_version;
  }
  return l.product_id < r.product_id;
}

}  // namespace trade_api
}  // namespace atframework
