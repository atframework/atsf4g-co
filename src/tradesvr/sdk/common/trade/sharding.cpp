// Copyright 2026 atframework
// Created by owent on 2026-11-26.

#include "trade/sharding.h"

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.trade.common.pb.h>
#include <protocol/config/com.struct.trade.config.pb.h>
#include <protocol/config/svr.hpa.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <xxhash.h>

#include <utility/random_engine.h>

#include <config/excel/config_easy_api.h>

#include <logic/hpa/logic_hpa_easy_api.h>
#include <logic/logic_server_setup.h>

#include <cstdint>
#include <cstring>
#include <utility>

#include "trade/trade_algorithm.h"

namespace atframework {
namespace trade_api {

namespace {
constexpr size_t kShardingSize = static_cast<size_t>(atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE);

template <class T>
inline static void copy_buffer_into(char* start, size_t offset, T value) {
  memcpy(start + offset, &value, sizeof(value));
}

static uint64_t make_area_hash_key(int32_t area_code, int32_t local_id) noexcept {
  // 保留负数 ID 在原有哈希输入中的符号扩展。
  return (static_cast<uint64_t>(area_code) << 32U) | static_cast<uint64_t>(local_id);
}
}  // namespace

static_assert(0U == (kShardingSize & (kShardingSize - 1U)), "Invalid EN_TRADE_CONST_SETTING_SHARDING_SIZE");

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_market_controller(
    int32_t area_code, int32_t trade_calculate_pool_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, trade_calculate_pool_id), 100002789};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrOrderFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_sales_slot(
    int32_t area_code, int32_t slot_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 938537009};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrOrderFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_purchase_slot(
    int32_t area_code, int32_t slot_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 938533913};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrOrderFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_auction_slot(
    int32_t area_code, int32_t slot_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 370248451};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrOrderFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node>
get_server_node_of_standard_price_controller_slot(int32_t area_code, int32_t slot_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 30000023};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrOrderFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_main_server_node_of_matchmaking(
    const atfw::trade::DTradeMarketProductKey& product_key) {
  return get_server_node_of_matchmaking(product_key, 0);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node>
get_random_replicated_server_node_of_matchmaking(const atfw::trade::DTradeMarketProductKey& product_key,
                                                 int32_t& sharding_count) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  auto index_set = mod->get_discovery_index_by_type(
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrMatchmakingFieldNumber);
  if (!index_set) {
    return nullptr;
  }

  const auto* metadata =
      logic_hpa_discovery_select(atfw::shared::config::logic_discovery_selector_cfg::kTradesvrMatchmakingFieldNumber,
                                 logic_hpa_discovery_select_mode::kReady);
  const auto& sorted_nodes = index_set->get_sorted_nodes(metadata);

  if (sorted_nodes.empty()) {
    return nullptr;
  }
  if (sorted_nodes.size() == 1) {
    sharding_count = 1;
    return sorted_nodes[0];
  }
  const int32_t sorted_node_count = static_cast<int32_t>(sorted_nodes.size());
  if (sharding_count > sorted_node_count) {
    sharding_count = sorted_node_count;
  }

  int32_t parallel_index = atfw::component::random_engine::random_between<int32_t>(0, sharding_count);
  return get_server_node_of_matchmaking(product_key, parallel_index);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_matchmaking(
    const atfw::trade::DTradeMarketProductKey& product_key, int32_t parallel_index) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  size_t idx = product_key_hash_type()(product_key, parallel_index, 938536639);

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrMatchmakingFieldNumber, idx);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_index_slot(
    int32_t area_code, int32_t slot_id, logic_hpa_discovery_select_mode mode) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 800000027};

  return mod->get_discovery_consistent_hash_node_by_type(
      mode, atfw::shared::config::logic_discovery_selector_cfg::kTradesvrIndexFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_search_slot(
    int32_t area_code, int32_t slot_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 800000041};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrSearchFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_recommend_slot(
    int32_t area_code, int32_t slot_id) {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return nullptr;
  }

  // 随便搞个素数作magic number
  uint64_t hash_target[] = {make_area_hash_key(area_code, slot_id), 800000201};

  return mod->get_discovery_consistent_hash_node_by_type(
      logic_hpa_discovery_select_mode::kReady,
      atfw::shared::config::logic_discovery_selector_cfg::kTradesvrSearchFieldNumber, hash_target);
}

TRADE_SDK_COMMON_API size_t get_sales_slot_count(const atfw::trade::DTradeMarketProductKey& /*product_key*/) {
  int32_t count = logic_config::me()->get_logic_cfg().trade().sales_order_slot_count();
  if (count <= 0) {
    return 4;
  }

  return static_cast<size_t>(count);
}

TRADE_SDK_COMMON_API int32_t get_sales_slot_id(const atfw::trade::DTradeMarketProductKey& product_key, size_t index) {
  size_t idx = product_key_hash_type()(product_key, index, 938537009);
  return static_cast<int32_t>(idx & (kShardingSize - 1U)) + 1;
}

TRADE_SDK_COMMON_API size_t get_purchase_slot_count(const atfw::trade::DTradeMarketProductKey& /*product_key*/) {
  int32_t count = logic_config::me()->get_logic_cfg().trade().purchase_order_slot_count();
  if (count <= 0) {
    return 4;
  }

  return static_cast<size_t>(count);
}

TRADE_SDK_COMMON_API int32_t get_purchase_slot_id(const atfw::trade::DTradeMarketProductKey& product_key,
                                                  size_t index) {
  size_t idx = product_key_hash_type()(product_key, index, 938533913);
  return static_cast<int32_t>(idx & (kShardingSize - 1U)) + 1;
}

TRADE_SDK_COMMON_API size_t get_auction_slot_count(const atfw::trade::DTradeMarketProductKey& /*product_key*/) {
  int32_t count = logic_config::me()->get_logic_cfg().trade().auction_order_slot_count();
  if (count <= 0) {
    return 4;
  }

  return static_cast<size_t>(count);
}

TRADE_SDK_COMMON_API int32_t get_auction_slot_id(const atfw::trade::DTradeMarketProductKey& product_key, size_t index) {
  size_t idx = product_key_hash_type()(product_key, index, 370248451);
  return static_cast<int32_t>(idx & (kShardingSize - 1U)) + 1;
}

TRADE_SDK_COMMON_API int32_t
get_standard_price_controller_slot_id(const atfw::trade::DTradeMarketProductKey& product_key) {
  if (product_key.trade_market_id() <= 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_INVALID_PRODUCT;
  }

  auto market_cfg = excel::get_ExcelTradeMarket_by_id(product_key.trade_market_id());
  if (!market_cfg) {
    return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_MARKET_NOT_FOUND_IN_TABLE;
  }

  size_t idx = product_key_hash_type()(product_key.area_code(), product_key.trade_product_id(),
                                       market_cfg->calculate_pool_id(), 380248459);
  return static_cast<int32_t>(idx & (kShardingSize - 1U)) + 1;
}

TRADE_SDK_COMMON_API int32_t make_sharding_slots(atfw::trade::DTradeOrderTicketMeta& inout) {
  if (inout.slot_id() != 0) {
    return PROJECT_NAMESPACE_ID::EN_SUCCESS;
  }

  if (inout.has_purchase_order()) {
    size_t slot_count = get_purchase_slot_count(inout.trade_product_key());
    if (0 == slot_count) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE;
    }

    int32_t slot_id = get_purchase_slot_id(inout.trade_product_key(),
                                           atfw::component::random_engine::random_between<size_t>(0, slot_count));
    if (slot_id < 0) {
      return slot_id;
    }
    if (slot_id == 0) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_INVALID_PRODUCT;
    }

    inout.set_slot_id(slot_id);
    return 0;
  }

  if (inout.has_sales_order()) {
    size_t slot_count = get_sales_slot_count(inout.trade_product_key());
    if (0 == slot_count) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE;
    }

    int32_t slot_id = get_sales_slot_id(inout.trade_product_key(),
                                        atfw::component::random_engine::random_between<size_t>(0, slot_count));
    if (slot_id < 0) {
      return slot_id;
    }
    if (slot_id == 0) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_INVALID_PRODUCT;
    }

    inout.set_slot_id(slot_id);
    return 0;
  }

  if (inout.has_auction_order()) {
    size_t slot_count = get_auction_slot_count(inout.trade_product_key());
    if (0 == slot_count) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_SERVICE_UNAVAILABLE;
    }

    int32_t slot_id = get_auction_slot_id(inout.trade_product_key(),
                                          atfw::component::random_engine::random_between<size_t>(0, slot_count));
    if (slot_id < 0) {
      return slot_id;
    }
    if (slot_id == 0) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TRADE_INVALID_PRODUCT;
    }

    inout.set_slot_id(slot_id);
    return 0;
  }

  return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
}

TRADE_SDK_COMMON_API int32_t get_index_main_slot(const atfw::trade::DTradeMarketProductKey& product_key) {
  int32_t index_group_count = logic_config::me()->get_logic_cfg().trade().index_group_count();
  if (index_group_count <= 0) {
    index_group_count = 16;
  }

  int32_t index_group_main_slot_count = logic_config::me()->get_logic_cfg().trade().index_group_main_slot_count();
  if (index_group_main_slot_count <= 0) {
    index_group_main_slot_count = 16;
  }
  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / index_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }
  if (index_group_main_slot_count > group_slot_count) {
    index_group_main_slot_count = group_slot_count;
  }

  // 随便搞个素数作magic number
  char hash_buffer[sizeof(int32_t) * 2];
  copy_buffer_into(hash_buffer, 0, product_key.trade_market_id());
  copy_buffer_into(hash_buffer, sizeof(int32_t), product_key.trade_product_id());

  uint64_t value = static_cast<uint64_t>(XXH64(hash_buffer, sizeof(hash_buffer), 800000027));

  const uint32_t slots_per_group = static_cast<uint32_t>(group_slot_count);
  uint32_t group_index = static_cast<uint32_t>(((value >> 32U) & 0x7fffffffU) % index_group_count);
  uint32_t main_slot_index = static_cast<uint32_t>((value & 0x7fffffffU) % index_group_main_slot_count);
  if (main_slot_index >= slots_per_group) {
    main_slot_index %= slots_per_group;
  }
  return static_cast<int32_t>((slots_per_group * group_index) + main_slot_index + 1U);
}

TRADE_SDK_COMMON_API std::pair<EnIndexSlotType, int32_t> get_index_slot_type(
    int32_t index_slot_id, const atfw::trade::DTradeMarketProductKey& product_key) {
  std::pair<EnIndexSlotType, int32_t> ret;

  if (index_slot_id <= 0 || index_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    ret.second = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    return ret;
  }

  int32_t main_slot_id = get_index_main_slot(product_key);
  if (main_slot_id <= 0 || main_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    ret.second = PROJECT_NAMESPACE_ID::EN_ERR_TRADE_INVALID_PRODUCT;
    return ret;
  }

  int32_t group_id = get_index_slot_group_id(index_slot_id);
  int32_t main_group_id = get_index_slot_group_id(main_slot_id);
  if (group_id <= 0 || main_group_id <= 0) {
    ret.second = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    return ret;
  }

  if (group_id != main_group_id) {
    ret.second = PROJECT_NAMESPACE_ID::EN_ERR_TRADE_INVALID_AREA_CODE;
    return ret;
  }

  ret.first = (main_slot_id == index_slot_id ? EnIndexSlotType::kMain : EnIndexSlotType::kReplicate);
  return ret;
}

TRADE_SDK_COMMON_API bool is_main_index_slot(int32_t index_slot_id) {
  if (index_slot_id <= 0 || index_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    return false;
  }

  int32_t index_group_count = logic_config::me()->get_logic_cfg().trade().index_group_count();
  if (index_group_count <= 0) {
    index_group_count = 16;
  }

  int32_t index_group_main_slot_count = logic_config::me()->get_logic_cfg().trade().index_group_main_slot_count();
  if (index_group_main_slot_count <= 0) {
    index_group_main_slot_count = 16;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / index_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }
  if (index_group_main_slot_count > group_slot_count) {
    index_group_main_slot_count = group_slot_count;
  }

  return ((index_slot_id - 1) % group_slot_count) < index_group_main_slot_count;
}

TRADE_SDK_COMMON_API int32_t get_index_slot_group_id(int32_t index_slot_id) {
  if (index_slot_id <= 0 || index_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    return 0;
  }

  int32_t index_group_count = logic_config::me()->get_logic_cfg().trade().index_group_count();
  if (index_group_count <= 0) {
    index_group_count = 16;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / index_group_count;
  return static_cast<int32_t>(((index_slot_id - 1) / group_slot_count) + 1);
}

TRADE_SDK_COMMON_API int32_t get_index_slot_group_id(const atfw::trade::DTradeMarketProductKey& product_key) {
  return get_index_slot_group_id(get_index_main_slot(product_key));
}

TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_slot_id_range_in_group_id(int32_t index_group_id) {
  if (index_group_id <= 0 || index_group_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    return {0, 0};
  }

  int32_t index_group_count = logic_config::me()->get_logic_cfg().trade().index_group_count();
  if (index_group_count <= 0) {
    index_group_count = 16;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / index_group_count;
  int32_t start_slot_id = 1 + (group_slot_count * (index_group_id - 1));
  return {start_slot_id, start_slot_id + group_slot_count - 1};
}

TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_group_id_range() {
  int32_t index_group_count = logic_config::me()->get_logic_cfg().trade().index_group_count();
  if (index_group_count <= 0) {
    index_group_count = 16;
  }

  return {1, index_group_count};
}

TRADE_SDK_COMMON_API int32_t get_index_random_slot_of_group_id(int32_t index_group_id,
                                                               EnIndexSlotType index_slot_type) {
  auto interval = get_index_slot_id_range_of_group(index_group_id, index_slot_type);

  if (interval.first >= interval.second) {
    return 0;
  }

  return atfw::component::random_engine::random_between(interval.first, interval.second);
}

int32_t get_index_random_slot(const atfw::trade::DTradeMarketProductKey& product_key, EnIndexSlotType index_slot_type) {
  return get_index_random_slot_of_group_id(get_index_slot_group_id(get_index_main_slot(product_key)), index_slot_type);
}

TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_slot_id_range_of_group(int32_t index_group_id,
                                                                                  EnIndexSlotType index_slot_type) {
  std::pair<int32_t, int32_t> interval;

  if (index_group_id <= 0) {
    return interval;
  }

  int32_t index_group_count = logic_config::me()->get_logic_cfg().trade().index_group_count();
  if (index_group_count <= 0) {
    index_group_count = 16;
  }

  int32_t index_group_main_slot_count = logic_config::me()->get_logic_cfg().trade().index_group_main_slot_count();
  if (index_group_main_slot_count <= 0) {
    index_group_main_slot_count = 16;
  }

  if (index_group_id > index_group_count) {
    return interval;
  }

  auto group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / index_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (index_group_main_slot_count > group_slot_count) {
    index_group_main_slot_count = group_slot_count;
  }

  switch (index_slot_type) {
    case EnIndexSlotType::kMain: {
      interval.first = group_slot_count * (index_group_id - 1);
      interval.second = interval.first + index_group_main_slot_count;
      break;
    }
    case EnIndexSlotType::kReplicate: {
      interval.first = group_slot_count * (index_group_id - 1);
      interval.second = interval.first + group_slot_count;
      interval.first += index_group_main_slot_count;
      break;
    }
    default: {
      interval.first = group_slot_count * (index_group_id - 1);
      interval.second = interval.first + group_slot_count;
      break;
    }
  }

  interval.first++;
  return interval;
}

TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_slot_id_range(
    const atfw::trade::DTradeMarketProductKey& product_key, EnIndexSlotType index_slot_type) {
  return get_index_slot_id_range_of_group(get_index_slot_group_id(get_index_main_slot(product_key)), index_slot_type);
}

TRADE_SDK_COMMON_API bool is_main_search_view_index_slot(int32_t search_slot_id) {
  if (search_slot_id <= 0 || search_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    return false;
  }

  int32_t search_group_count = logic_config::me()->get_logic_cfg().trade().search_group_count();
  if (search_group_count <= 0) {
    search_group_count = 32;
  }

  int32_t search_group_main_slot_count = logic_config::me()->get_logic_cfg().trade().search_group_main_slot_count();
  if (search_group_main_slot_count <= 0) {
    search_group_main_slot_count = 4;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / search_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (search_group_main_slot_count > group_slot_count) {
    search_group_main_slot_count = group_slot_count;
  }

  return ((search_slot_id - 1) % group_slot_count) < search_group_main_slot_count;
}

TRADE_SDK_COMMON_API int32_t get_search_random_slot(const atfw::trade::DTradeOrderTicketSearchRule& search_rule,
                                                    EnSearchSlotType search_slot_type) {
  if (search_rule.trade_market_id() <= 0) {
    return 0;
  }

  int32_t search_group_count = logic_config::me()->get_logic_cfg().trade().search_group_count();
  if (search_group_count <= 0) {
    search_group_count = 32;
  }

  int32_t search_group_main_slot_count = logic_config::me()->get_logic_cfg().trade().search_group_main_slot_count();
  if (search_group_main_slot_count <= 0) {
    search_group_main_slot_count = 4;
  }

  int32_t group_slot_count =
      static_cast<int32_t>(atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / search_group_count);
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (search_group_main_slot_count > group_slot_count) {
    search_group_main_slot_count = group_slot_count;
  }

  // 随便搞个素数作magic number
  char hash_buffer[sizeof(int32_t) * 3];
  copy_buffer_into(hash_buffer, 0, search_rule.trade_market_id());
  copy_buffer_into(hash_buffer, sizeof(int32_t), search_rule.major_index_type());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, search_rule.minor_index_type());
  uint64_t value = static_cast<uint64_t>(XXH64(hash_buffer, sizeof(hash_buffer), 800000029));
  int32_t search_group_idx =
      static_cast<int32_t>(static_cast<uint32_t>(value & 0x7fffffffU) % static_cast<uint32_t>(search_group_count));

  int32_t start_slot = 0;
  int32_t end_slot = 0;

  switch (search_slot_type) {
    case EnSearchSlotType::kMain: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + search_group_main_slot_count;
      break;
    }
    case EnSearchSlotType::kReplicate: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      start_slot += search_group_main_slot_count;
      break;
    }
    default: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      break;
    }
  }

  if (start_slot >= end_slot) {
    return 0;
  }

  return atfw::component::random_engine::random_between(start_slot, end_slot) + 1;
}

TRADE_SDK_COMMON_API int32_t get_search_random_slot(int32_t index_group_id, int32_t area_code, int32_t trade_market_id,
                                                    int32_t order_type, EnSearchSlotType search_slot_type) {
  if (index_group_id <= 0 || order_type <= 0 || trade_market_id <= 0) {
    return 0;
  }

  int32_t search_group_count = logic_config::me()->get_logic_cfg().trade().search_group_count();
  if (search_group_count <= 0) {
    search_group_count = 32;
  }

  int32_t search_group_main_slot_count = logic_config::me()->get_logic_cfg().trade().search_group_main_slot_count();
  if (search_group_main_slot_count <= 0) {
    search_group_main_slot_count = 4;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / search_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (search_group_main_slot_count > group_slot_count) {
    search_group_main_slot_count = group_slot_count;
  }

  // 随便搞个素数作magic number
  char hash_buffer[sizeof(int32_t) * 4];
  copy_buffer_into(hash_buffer, 0, index_group_id);
  copy_buffer_into(hash_buffer, sizeof(int32_t), area_code);
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, trade_market_id);
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, order_type);
  uint64_t value = static_cast<uint64_t>(XXH64(hash_buffer, sizeof(hash_buffer), 800000029));
  int32_t search_group_idx =
      static_cast<int32_t>(static_cast<uint32_t>(value & 0x7fffffffU) % static_cast<uint32_t>(search_group_count));

  int32_t start_slot = 0;
  int32_t end_slot = 0;

  switch (search_slot_type) {
    case EnSearchSlotType::kMain: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + search_group_main_slot_count;
      break;
    }
    case EnSearchSlotType::kReplicate: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      start_slot += search_group_main_slot_count;
      break;
    }
    default: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      break;
    }
  }
  if (start_slot >= end_slot) {
    return 0;
  }

  return atfw::component::random_engine::random_between(start_slot, end_slot) + 1;
}

TRADE_SDK_COMMON_API bool is_main_recommend_order_ticket_slot(int32_t search_slot_id) {
  if (search_slot_id <= 0 || search_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    return false;
  }

  int32_t order_ticket_recommend_group_count =
      logic_config::me()->get_logic_cfg().trade().order_ticket_recommend_group_count();
  if (order_ticket_recommend_group_count <= 0) {
    order_ticket_recommend_group_count = 32;
  }

  int32_t order_ticket_recommend_group_main_slot_count =
      logic_config::me()->get_logic_cfg().trade().order_ticket_recommend_group_main_slot_count();
  if (order_ticket_recommend_group_main_slot_count <= 0) {
    order_ticket_recommend_group_main_slot_count = 4;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / order_ticket_recommend_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (order_ticket_recommend_group_main_slot_count > group_slot_count) {
    order_ticket_recommend_group_main_slot_count = group_slot_count;
  }

  return ((search_slot_id - 1) % group_slot_count) < order_ticket_recommend_group_main_slot_count;
}

TRADE_SDK_COMMON_API int32_t get_recommend_order_ticket_random_slot(int32_t trade_market_id, int32_t order_ticket_type,
                                                                    EnSearchSlotType search_slot_type) {
  if (trade_market_id <= 0 || order_ticket_type <= 0) {
    return 0;
  }

  int32_t order_ticket_recommend_group_count =
      logic_config::me()->get_logic_cfg().trade().order_ticket_recommend_group_count();
  if (order_ticket_recommend_group_count <= 0) {
    order_ticket_recommend_group_count = 32;
  }

  int32_t order_ticket_recommend_group_main_slot_count =
      logic_config::me()->get_logic_cfg().trade().order_ticket_recommend_group_main_slot_count();
  if (order_ticket_recommend_group_main_slot_count <= 0) {
    order_ticket_recommend_group_main_slot_count = 4;
  }

  int32_t group_slot_count =
      static_cast<int32_t>(atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / order_ticket_recommend_group_count);
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (order_ticket_recommend_group_main_slot_count > group_slot_count) {
    order_ticket_recommend_group_main_slot_count = group_slot_count;
  }

  // 随便搞个素数作magic number
  char hash_buffer[sizeof(int32_t) * 2];
  copy_buffer_into(hash_buffer, 0, trade_market_id);
  copy_buffer_into(hash_buffer, sizeof(int32_t), order_ticket_type);
  uint64_t value = static_cast<uint64_t>(XXH64(hash_buffer, sizeof(hash_buffer), 803000031));
  int32_t search_group_idx = static_cast<int32_t>(static_cast<uint32_t>(value & 0x7fffffffU) %
                                                  static_cast<uint32_t>(order_ticket_recommend_group_count));

  int32_t start_slot = 0;
  int32_t end_slot = 0;

  switch (search_slot_type) {
    case EnSearchSlotType::kMain: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + order_ticket_recommend_group_main_slot_count;
      break;
    }
    case EnSearchSlotType::kReplicate: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      start_slot += order_ticket_recommend_group_main_slot_count;
      break;
    }
    default: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      break;
    }
  }

  if (start_slot >= end_slot) {
    return 0;
  }

  return atfw::component::random_engine::random_between(start_slot, end_slot) + 1;
}

TRADE_SDK_COMMON_API bool is_main_search_order_cache_slot(int32_t search_slot_id) {
  if (search_slot_id <= 0 || search_slot_id > atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE) {
    return false;
  }

  int32_t order_ticket_group_count = logic_config::me()->get_logic_cfg().trade().order_ticket_group_count();
  if (order_ticket_group_count <= 0) {
    order_ticket_group_count = 32;
  }

  int32_t order_ticket_group_main_slot_count =
      logic_config::me()->get_logic_cfg().trade().order_ticket_group_main_slot_count();
  if (order_ticket_group_main_slot_count <= 0) {
    order_ticket_group_main_slot_count = 4;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / order_ticket_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (order_ticket_group_main_slot_count > group_slot_count) {
    order_ticket_group_main_slot_count = group_slot_count;
  }

  return ((search_slot_id - 1) % group_slot_count) < order_ticket_group_main_slot_count;
}

TRADE_SDK_COMMON_API int32_t get_search_random_slot(const atfw::trade::trade_order_ticket_key& order_key,
                                                    EnSearchSlotType search_slot_type) {
  if (order_key.trade_order_ticket_id() <= 0 || order_key.order_type() <= 0) {
    return 0;
  }

  int32_t order_ticket_group_count = logic_config::me()->get_logic_cfg().trade().order_ticket_group_count();
  if (order_ticket_group_count <= 0) {
    order_ticket_group_count = 32;
  }

  int32_t order_ticket_group_main_slot_count =
      logic_config::me()->get_logic_cfg().trade().order_ticket_group_main_slot_count();
  if (order_ticket_group_main_slot_count <= 0) {
    order_ticket_group_main_slot_count = 4;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / order_ticket_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  if (order_ticket_group_main_slot_count > group_slot_count) {
    order_ticket_group_main_slot_count = group_slot_count;
  }

  // 随便搞个素数作magic number
  char hash_buffer[(sizeof(int32_t) * 2) + sizeof(int64_t)];
  copy_buffer_into(hash_buffer, 0, order_key.area_code());
  copy_buffer_into(hash_buffer, sizeof(int32_t), order_key.order_type());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, order_key.trade_order_ticket_id());
  uint64_t value = static_cast<uint64_t>(XXH64(hash_buffer, sizeof(hash_buffer), 800000029));
  int32_t search_group_idx = static_cast<int32_t>(static_cast<uint32_t>(value & 0x7fffffffU) %
                                                  static_cast<uint32_t>(order_ticket_group_count));

  int32_t start_slot = 0;
  int32_t end_slot = 0;

  switch (search_slot_type) {
    case EnSearchSlotType::kMain: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + order_ticket_group_main_slot_count;
      break;
    }
    case EnSearchSlotType::kReplicate: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      start_slot += order_ticket_group_main_slot_count;
      break;
    }
    default: {
      start_slot = group_slot_count * search_group_idx;
      end_slot = start_slot + group_slot_count;
      break;
    }
  }

  if (start_slot >= end_slot) {
    return 0;
  }

  return atfw::component::random_engine::random_between(start_slot, end_slot) + 1;
}

TRADE_SDK_COMMON_API int32_t get_search_random_slot(const atfw::trade::DTradeMarketProductKey& product_key) {
  if (product_key.trade_market_id() <= 0 || product_key.trade_product_id() <= 0) {
    return 0;
  }

  int32_t market_product_group_count = logic_config::me()->get_logic_cfg().trade().market_product_group_count();
  if (market_product_group_count <= 0) {
    market_product_group_count = 32;
  }

  int32_t group_slot_count = atfw::trade::EN_TRADE_CONST_SETTING_SHARDING_SIZE / market_product_group_count;
  if (group_slot_count <= 0) {
    group_slot_count = 1;
  }

  // 随便搞个素数作magic number
  char hash_buffer[sizeof(int32_t) * 3];
  copy_buffer_into(hash_buffer, 0, product_key.area_code());
  copy_buffer_into(hash_buffer, sizeof(int32_t), product_key.trade_market_id());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, product_key.trade_product_id());
  uint64_t value = static_cast<uint64_t>(XXH64(hash_buffer, sizeof(hash_buffer), 800000249));
  int32_t search_group_idx = static_cast<int32_t>(static_cast<uint32_t>(value & 0x7fffffffU) %
                                                  static_cast<uint32_t>(market_product_group_count));

  int32_t start_slot = group_slot_count * search_group_idx;
  int32_t end_slot = start_slot + group_slot_count;

  if (start_slot >= end_slot) {
    return 0;
  }

  return atfw::component::random_engine::random_between(start_slot, end_slot) + 1;
}

}  // namespace trade_api
}  // namespace atframework
