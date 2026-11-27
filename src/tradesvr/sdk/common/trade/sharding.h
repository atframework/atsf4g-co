// Copyright 2026 atframework
// Created by owent on 2026-11-26.

#pragma once

#include <memory/rc_ptr.h>

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

#include <atframe/etcdcli/etcd_discovery.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <cstdint>
#include <utility>

namespace atframework {
namespace trade_api {

enum class EnIndexSlotType : int32_t {
  kAll = 0,
  kMain = 1,
  kReplicate = 2,
};

enum class EnSearchSlotType : int32_t {
  kAll = 0,
  kMain = 1,
  kReplicate = 2,
};

using index_cycle_id_type = time_t;

/**
 * @brief 获取交易行控制服务节点（order服）
 *
 * @param area_code 区域码
 * @param trade_calculate_pool_id 交易行结算池ID
 * @return 交易行控制服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_market_controller(
    int32_t area_code, int32_t trade_calculate_pool_id);

/**
 * @brief 获取供货单的委托单服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 委托单服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_sales_slot(
    int32_t area_code, int32_t slot_id);

/**
 * @brief 获取订购单的委托单服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 委托单服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_purchase_slot(
    int32_t area_code, int32_t slot_id);

/**
 * @brief 获取竞拍单的委托单服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 委托单服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_auction_slot(
    int32_t area_code, int32_t slot_id);

/**
 * @brief 获取商品价格池管理服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 委托单服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node>
get_server_node_of_standard_price_controller_slot(int32_t area_code, int32_t slot_id);

/**
 * @brief 获取撮合服务商品主节点
 *
 * @param product_key 订单信息
 * @return 撮合服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_main_server_node_of_matchmaking(
    const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取撮合服务商品随机从节点，如果不存在则返回主节点
 *
 * @param product_key 订单信息
 * @return 撮合服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node>
get_random_replicated_server_node_of_matchmaking(const atfw::trade::DTradeMarketProductKey& product_key,
                                                 int32_t& sharding_count);

/**
 * @brief 获取撮合服务节点
 *
 * @param product_key 订单信息
 * @param parallel_index parallel_index
 * @return 撮合服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_matchmaking(
    const atfw::trade::DTradeMarketProductKey& product_key, int32_t parallel_index);

/**
 * @brief 获取索引服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 索引服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_index_slot(
    int32_t area_code, int32_t slot_id, logic_hpa_discovery_select_mode mode);

/**
 * @brief 获取搜索服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 搜索服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_search_slot(
    int32_t area_code, int32_t slot_id);

/**
 * @brief 获取推荐服务节点
 *
 * @param area_code 区域码
 * @param slot_id slot id
 * @return 推荐服务节点，失败或服务不可用返回 nullptr
 */
TRADE_SDK_COMMON_API atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_node> get_server_node_of_recommend_slot(
    int32_t area_code, int32_t slot_id);

/**
 * @brief 获取供货单的Slot数量
 *
 * @param product_key 供货单商品信息
 * @return 供货单的Slot数量
 */
TRADE_SDK_COMMON_API size_t get_sales_slot_count(const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取供货单的Slot ID
 *
 * @param product_key 供货单商品信息
 * @param index 下标: [0 - (get_sales_slot_count(product_key)-1)] (0为主节点)
 * @return 错误码或供货单的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_sales_slot_id(const atfw::trade::DTradeMarketProductKey& product_key, size_t index);

/**
 * @brief 获取订购单的Slot数量
 *
 * @param product_key 订购单商品信息
 * @return 订购单的Slot数量
 */
TRADE_SDK_COMMON_API size_t get_purchase_slot_count(const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取订购单的Slot ID
 *
 * @param product_key 订购单商品信息
 * @param index 下标: [0 - (get_purchase_slot_count(product_key)-1)] (0为主节点)
 * @return 错误码或订购单的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_purchase_slot_id(const atfw::trade::DTradeMarketProductKey& product_key, size_t index);

/**
 * @brief 获取竞拍单的Slot数量
 *
 * @param product_key 竞拍单商品信息
 * @return 竞拍单的Slot数量
 */
TRADE_SDK_COMMON_API size_t get_auction_slot_count(const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取竞拍单的Slot ID
 *
 * @param product_key 竞拍单商品信息
 * @param index 下标: [0 - (get_auction_slot_count(product_key)-1)] (0为主节点)
 * @return 错误码或竞拍单的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_auction_slot_id(const atfw::trade::DTradeMarketProductKey& product_key, size_t index);

/**
 * @brief 获取商品价格池管理服务的Slot ID
 *
 * @param product_key 供货单商品信息
 * @return 错误码或供货单的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t
get_standard_price_controller_slot_id(const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 根据委托单填充slot信息
 *
 * @param inout 委托单
 * @return 错误码或0
 */
TRADE_SDK_COMMON_API int32_t make_sharding_slots(atfw::trade::DTradeOrderTicketMeta& inout);

/**
 * @brief 获取指定商品的索引服务的Main Slot ID
 *
 * @param product_key 订购单商品信息
 * @return 错误码或索引服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_index_main_slot(const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取指定Slot ID与商品的归属关系
 *
 * @param index_slot_id 索引服务的Slot ID,从1开始
 * @param product_key 商品信息
 * @return 索引服务 Slot 类型与错误码构成的pair
 */
TRADE_SDK_COMMON_API std::pair<EnIndexSlotType, int32_t> get_index_slot_type(
    int32_t index_slot_id, const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取指定索引服务 Slot ID 是否是Main Slot
 *
 * @param index_slot_id 索引服务的Slot ID,从1开始
 * @return 是主索引服务 Slot ID则返回true
 */
TRADE_SDK_COMMON_API bool is_main_index_slot(int32_t index_slot_id);

/**
 * @brief 获取指定索引服务 Slot ID的分组ID
 *
 * @param index_slot_id 索引服务的Slot ID,从1开始
 * @return 分组ID,index_slot_id无效返回0，否则返回分组ID(>0)
 */
TRADE_SDK_COMMON_API int32_t get_index_slot_group_id(int32_t index_slot_id);

/**
 * @brief 获取特定商品所属的索引服务 分组ID
 *
 * @param product_key 订购单商品信息
 * @return 分组ID (> 0)，发生错误时返回0
 */
TRADE_SDK_COMMON_API int32_t get_index_slot_group_id(const atfw::trade::DTradeMarketProductKey& product_key);

/**
 * @brief 获取特定商品所属的索引服务 分组ID
 *
 * @param index_group_id 索引服务的Group ID,从1开始
 * @return [分组内Slot ID最小值, 分组内Slot ID最大值] (包含)
 */
TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_slot_id_range_in_group_id(int32_t index_group_id);

/**
 * @brief 获取索引服务分组ID范围
 *
 * @return [分组ID最小值,分组ID最大值] (包含)
 */
TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_group_id_range();

/**
 * @brief 获取指定索引服务 Group ID内的随机Slot ID
 *
 * @param index_group_id 索引服务的Group ID,从1开始
 * @param index_slot_type Slot类型
 * @return Slot ID, index_group_id无效返回0，否则返回该分组内的随机 Slot ID
 */
TRADE_SDK_COMMON_API int32_t get_index_random_slot_of_group_id(int32_t index_group_id,
                                                               EnIndexSlotType index_slot_type = EnIndexSlotType::kAll);

/**
 * @brief 获取指定商品的索引服务的随机 Slot ID
 *
 * @param product_key 订购单商品信息
 * @param index_slot_type Slot类型
 * @return 错误码或索引服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_index_random_slot(const atfw::trade::DTradeMarketProductKey& product_key,
                                                   EnIndexSlotType index_slot_type = EnIndexSlotType::kAll);

/**
 * @brief 获取指定搜索服务的视图索引 Slot ID 是否是Main Slot
 *
 * @param index_group_id 索引服务的Group ID,从1开始
 * @param index_slot_type Slot类型
 * @return SlotID分布半开半闭区间，[min_id, max_id)
 */
TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_slot_id_range_of_group(
    int32_t index_group_id, EnIndexSlotType index_slot_type = EnIndexSlotType::kAll);

/**
 * @brief 获取指定商品的归属索引 Slot_ID
 *
 * @param product_key 订购单商品信息
 * @param index_slot_type Slot类型
 * @return SlotID分布半开半闭区间，[min_id, max_id)
 */
TRADE_SDK_COMMON_API std::pair<int32_t, int32_t> get_index_slot_id_range(
    const atfw::trade::DTradeMarketProductKey& product_key, EnIndexSlotType index_slot_type = EnIndexSlotType::kAll);

/**
 * @brief 获取指定搜索服务 Slot ID 是否是Main Slot
 *
 * @param search_slot_id 搜索服务的Slot ID,从1开始
 * @return 是主搜索服务 Slot ID则返回true
 */
TRADE_SDK_COMMON_API bool is_main_search_view_index_slot(int32_t search_slot_id);

/**
 * @brief 获取指定商品的搜索服务的随机 Slot ID
 *
 * @param search_rule 订购单商品信息
 * @param search_slot_type Slot类型
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_search_random_slot(const atfw::trade::DTradeOrderTicketSearchRule& search_rule,
                                                    EnSearchSlotType search_slot_type = EnSearchSlotType::kAll);

/**
 * @brief 获取指定索引分组的的搜索服务的随机 Slot ID
 *
 * @param index_group_id 索引服务分组ID
 * @param area_code 交易行区域ID
 * @param trade_market_id 交易行ID
 * @param order_type 订单类型
 * @param search_slot_type Slot类型
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_search_random_slot(int32_t index_group_id, int32_t area_code, int32_t trade_market_id,
                                                    int32_t order_type, EnSearchSlotType search_slot_type);

/**
 * @brief 获取指定订单推荐服务 Slot ID 是否是Main Slot
 *
 * @param search_slot_id 订单推荐服务的Slot ID,从1开始
 * @return 是主订单推荐服务 Slot ID则返回true
 */
TRADE_SDK_COMMON_API bool is_main_recommend_order_ticket_slot(int32_t search_slot_id);

/**
 * @brief 获取指定订单推荐服务的随机 Slot ID
 *
 * @param trade_market_id 商城ID
 * @param order_ticket_type 订单类型
 * @param search_slot_type Slot类型
 * @return 错误码或订单推荐服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_recommend_order_ticket_random_slot(
    int32_t trade_market_id, int32_t order_ticket_type, EnSearchSlotType search_slot_type = EnSearchSlotType::kAll);

/**
 * @brief 获取指定搜索服务的订单缓存 Slot ID 是否是Main Slot
 *
 * @param search_slot_id 搜索服务的Slot ID,从1开始
 * @return 是主搜索服务 Slot ID则返回true
 */
TRADE_SDK_COMMON_API bool is_main_search_order_cache_slot(int32_t search_slot_id);

/**
 * @brief 获取指定商品委托单的搜索服务的随机 Slot ID
 *
 * @param order_key 委托单Key
 * @param search_slot_type Slot类型
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_search_random_slot(const atfw::trade::trade_order_ticket_key& order_key,
                                                    EnSearchSlotType search_slot_type = EnSearchSlotType::kAll);

/**
 * @brief 获取指定商品的搜索服务的随机 Slot ID
 *
 * @param product_key 商品Key
 * @return 错误码或搜索服务的Slot ID,从1开始
 */
TRADE_SDK_COMMON_API int32_t get_search_random_slot(const atfw::trade::DTradeMarketProductKey& product_key);
}  // namespace trade_api
}  // namespace atframework
