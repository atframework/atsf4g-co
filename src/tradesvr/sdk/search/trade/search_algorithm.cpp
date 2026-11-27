// Copyright 2026 atframework
// Created by owent on 2026-11-27.

#include "trade/search_algorithm.h"

#include <trade/sharding.h>

namespace atframework {
namespace trade_search {

TRADE_SDK_SEARCH_API int32_t get_random_slot_id(const atfw::trade::DTradeOrderTicketSearchRule& search_rule) {
  return atfw::trade_api::get_search_random_slot(search_rule, atfw::trade_api::EnSearchSlotType::kAll);
}

TRADE_SDK_SEARCH_API int32_t get_random_slot_id(const atfw::trade::trade_order_ticket_key& order_ticket_key) {
  return atfw::trade_api::get_search_random_slot(order_ticket_key, atfw::trade_api::EnSearchSlotType::kAll);
}

TRADE_SDK_SEARCH_API int32_t get_random_slot_id(const atfw::trade::DTradeMarketProductKey& product_key) {
  return atfw::trade_api::get_search_random_slot(product_key);
}

TRADE_SDK_SEARCH_API int64_t get_search_price(const atfw::trade::DTradeOrderTicketMeta& meta) noexcept {
  switch (meta.type_case()) {
    case atfw::trade::DTradeOrderTicketMeta::kPurchaseOrder:
      return meta.purchase_order().current_unit_price();
    case atfw::trade::DTradeOrderTicketMeta::kSalesOrder:
      return meta.sales_order().unit_price();
    case atfw::trade::DTradeOrderTicketMeta::kAuctionOrder:
      return meta.auction_order().auction_order_meta().current_price();
    default:
      break;
  }

  return 0;
}

}  // namespace trade_search
}  // namespace atframework
