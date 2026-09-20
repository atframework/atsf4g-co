// Copyright 2025 atframework

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"
#include "ItemAlgorithm/ItemContainer.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief 无限格子容器
///
/// 不限制行列数量, 每件道具按配置占一个位置 (忽略道具的 row/col 尺寸),
/// 只判断目标位置是否已被其它条目占用, 不做边界与矩形碰撞检查。
///
/// 本类只提供本模式的单条行为与自己的数据结构:
///   * 槽位索引 / GUID 索引的维护与占用判断 (position_index_ / guid_index_);
///   * 单条校验与单条落位的 on_*_one 钩子;
///   * 本模式特有的 move / check_move (基类没有腾挪流程)。
/// check_add / add / check_sub / sub / check_has / load /
/// apply_entries / find_positions_for_basics / find_positions_for_instances 的**流程**都在
/// ItemContainer 基类里, 基类做完通用校验与循环后把每条交给下面的钩子。
///
/// 位置由本类维护的槽位索引表达, 寻位通过 on_find_position_for_infinite 钩子
/// 交给子类决定落位: 装备槽这类按 type_id 映射 slot_idx 的用法覆盖该钩子实现。
/// proto 位置字段的读写由最上层接入实现 (extract_position / apply_position), 库内不做映射。
class ATFW_UTIL_SYMBOL_VISIBLE ItemInfiniteGridContainer : public ItemContainer {
 public:
  using position_index_type =
      std::unordered_map<ItemGridPosition, item_entry_ptr_t, ItemGridPositionHash, ItemGridPositionEqualTo>;
  using guid_index_type = std::unordered_map<int64_t, item_entry_ptr_t>;
  using position_set_type = std::unordered_set<ItemGridPosition, ItemGridPositionHash, ItemGridPositionEqualTo>;

 public:
  ITEM_ALGORITHM_API ItemInfiniteGridContainer();
  ITEM_ALGORITHM_API ~ItemInfiniteGridContainer() override;

  ItemInfiniteGridContainer(const ItemInfiniteGridContainer&) = delete;
  ItemInfiniteGridContainer& operator=(const ItemInfiniteGridContainer&) = delete;
  ItemInfiniteGridContainer(ItemInfiniteGridContainer&&) = delete;
  ItemInfiniteGridContainer& operator=(ItemInfiniteGridContainer&&) = delete;

 public:
  /// @brief 初始化无限格子容器
  /// @param position_type  容器对应的位置字段
  /// @param container_guid 容器唯一 ID
  ITEM_ALGORITHM_API void init(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                               int64_t container_guid);

  // ---- 位置查询 ----
  /// @brief 取槽位上的条目
  ITEM_ALGORITHM_API item_entry_ptr_t get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const;
  /// @brief 按 GUID 取条目
  ITEM_ALGORITHM_API item_entry_ptr_t get_by_guid(int64_t guid) const;

  // proto 位置字段的读写 (extract_position / apply_position) 由最上层接入按自己的业务字段
  // 实现, 库内不再映射; 本模式内部一律用上面的 ItemGridPosition 坐标, 需要在库里取坐标时
  // 调用虚函数 extract_position, 需要写回请求时调用虚函数 apply_position。

  // ---- 移动 ----
  // check_move / move 的流程 (入参自持 / 初始化检查 / 空请求 / 两阶段循环 / 结果汇总) 在基类;
  // 本模式只实现移动语义钩子: on_check_move (Sub / Add 入参与槽位占用推演) 与
  // on_move_sub_one / on_move_add_one (摘来源索引扣数量、合入目标槽位或新建条目)。


 protected:
  // ============================================================
  // 批次校验状态
  // ============================================================

  /// @brief 本模式在一次批次校验期间累计的占位 / 扣减 / 对账状态
  ///
  /// 被调用时机: on_check_add / on_check_sub / on_check_has 各自开始时创建, 在本次批次校验内使用。
  /// check_add 需要它是因为同批次前面的请求可能已经占掉槽位或新建了条目;
  /// check_sub / check_has 需要它是因为同一 GUID / 槽位在一个批次里可能被要求多次。
  class InfiniteGridCheckScratch {
   public:
    /// @brief 本批次内将要新建的槽位
    struct PendingNewSlot {
      int32_t type_id = 0;            ///< 将要放进该槽位的道具类型
      int64_t accumulated_count = 0;  ///< 该槽位在本批次内累计放入的数量
    };

    /// @brief 本批次内要加到"已有槽位条目"上的额外数量 (键为槽位)
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_existing_extra;
    /// @brief 本批次内将要新建的槽位 (键为槽位)
    std::unordered_map<ItemGridPosition, PendingNewSlot, ItemGridPositionHash, ItemGridPositionEqualTo>
        pending_new_slots;
    /// @brief 本批次内已按 GUID 要求扣减过的 GUID (同一批次里同一个 GUID 不能扣两次)
    std::unordered_set<int64_t> pending_sub_guids;
    /// @brief 本批次内按槽位累计已要求扣减的数量 (键为槽位)
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_sub_counts;
    /// @brief 本批次内按 GUID 累计已要求对账的数量
    std::unordered_map<int64_t, int64_t> pending_has_guids;
    /// @brief 本批次内按槽位累计已要求对账的数量 (键为槽位)
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_has_positions;
  };

  /// @brief 一批放入请求的本模式校验
  ///
  /// 被调用时机: 基类 check_add 完成所有通用校验后调用一次。
  ITEM_ALGORITHM_API ItemOperationResult on_check_add(const ItemAddCheckedRequest& checked_request) const override;

  /// @brief 一批扣减请求的本模式校验
  ///
  /// 被调用时机: 基类 check_sub 完成所有通用校验和按类型总量校验后调用一次。
  ITEM_ALGORITHM_API ItemOperationResult on_check_sub(const ItemSubCheckedRequest& checked_request) const override;

  /// @brief 一批对账请求的本模式校验
  ///
  /// 被调用时机: 基类 check_has 完成所有通用校验和按类型总量校验后调用一次。
  ITEM_ALGORITHM_API ItemOperationResult on_check_has(const excel_config_group_ptr_t& config_group,
                                                      const item_basic_readable_iterable& requests) const override;

  // ============================================================
  // 单条执行钩子 — 基类负责 checked request 校验与循环, 这里只处理一件
  // ============================================================

  /// @brief 放入一件道具 (合入已有槽位条目, 或新建条目并挂上槽位索引)
  ///
  /// 被调用时机: 基类 add 逐条循环, checked request 已校验通过; load 也走这里。
  /// @param instance 本条待放入的道具 (只读)
  /// @param context 钩子上下文 (容器判定的原因 + 调用方透传的来源)
  ITEM_ALGORITHM_API ItemOperationResult on_add_one(ItemAddCheckedRequest& checked_request,
                                                    const PROJECT_NAMESPACE_ID::DItemInstance& instance,
                                                    const ItemOperationContext& context) override;

  /// @brief 扣减一件道具 (扣完为 0 就整体移除条目并摘掉槽位与 GUID 索引)
  ///
  /// 被调用时机: 基类 sub 逐条循环, checked request 已校验通过。
  /// @param request 本条待扣减的请求 (只读)
  ITEM_ALGORITHM_API ItemOperationResult on_sub_one(ItemSubCheckedRequest& checked_request,
                                                   const PROJECT_NAMESPACE_ID::DItemBasic& request,
                                                   const ItemOperationContext& context) override;

  /// @brief 移动请求的本模式校验 (Sub / Add 入参与 helper 字段, 再推演目标槽位占用与堆叠上限)
  ///
  /// 被调用时机: 基类 check_move 完成初始化检查与空请求判断后调用一次。
  /// 失败时把错误码写进 checked_request.result.error_code 并返回 false。
  ITEM_ALGORITHM_API bool on_check_move(ItemMoveCheckedRequest& checked_request) const override;

  /// @brief 扣减一条移动来源 (整体移走时摘掉槽位与 GUID 索引)
  ///
  /// 被调用时机: 基类 move 的 kMoveSub 阶段逐条循环, 每条一次。
  ITEM_ALGORITHM_API ItemOperationResult on_move_sub_one(ItemMoveCheckedRequest& checked_request,
                                                        const ItemMoveSubRequest& request,
                                                        const ItemOperationContext& context) override;

  /// @brief 放入一条移动目标 (目标槽位已有同类无 GUID 条目时合入, 否则新建条目并挂索引)
  ///
  /// 被调用时机: 基类 move 的 kMoveAdd 阶段逐条循环 (全部 kMoveSub 之后)。
  ITEM_ALGORITHM_API ItemOperationResult on_move_add_one(ItemMoveCheckedRequest& checked_request,
                                                        const ItemMoveAddRequest& request,
                                                        const ItemOperationContext& context) override;


  /// @brief 载入一条持久化数据 (同槽位同类无 GUID 条目合并, 否则新建)
  ///
  /// 被调用时机: 基类 load 通过通用校验 (条目字段 / 容器归属 / 位置字段 / 类型存在 / GUID 唯一 /
  /// 数量上限) 之后调用一次。载入的是已经存在的存档数据, 不做堆叠上限检查; 槽位被占用即失败。
  ITEM_ALGORITHM_API bool on_load_one(const excel_config_group_ptr_t& config_group,
                                      const PROJECT_NAMESPACE_ID::DItemInstance& item_instance,
                                      const ItemOperationContext& context) override;

  /// @brief 应用客户端同步的"删除一条" (摘索引 + 移出分组 + 通知数量变化)
  ///
  /// 被调用时机: 基类 apply_entries 的删除阶段, 每条已经找到的条目调用一次。
  ITEM_ALGORITHM_API void on_apply_remove_one(const excel_config_group_ptr_t& config_group,
                                              const item_entry_ptr_t& entry,
                                              const ItemOperationContext& context) override;

  /// @brief 应用客户端同步的"新增或更新一条"
  ///
  /// 被调用时机: 基类 apply_entries 的新增/更新阶段, 每条已通过通用校验之后调用一次。
  /// 已有条目位置变化时要重新排序 (先摘出分组, 刷排序键, 再插回) 并按新槽位重挂索引;
  /// 位置没变但 GUID 变了只需要重挂 GUID 索引; 条目不存在则新建并挂索引。
  /// @param existing 该 entry_id 已存在的条目 (不存在时为 nullptr)
  ITEM_ALGORITHM_API void on_apply_update_one(const excel_config_group_ptr_t& config_group,
                                              const item_entry_ptr_t& existing,
                                              const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update,
                                              const ItemOperationContext& context) override;

  /// @brief 为一批道具寻位 (本模式不扫描格子, 逐件委托 on_find_position_for_infinite)
  ///
  /// 被调用时机: 基类 find_positions_for_instances (及由它实现的 find_positions_for_basics)
  /// 校验入参并预留输出之后调用一次。
  /// 本模式没有格子与占用可推算, 无法表达"某个道具即将被消耗, 请把它占的槽位当成空闲",
  /// 因此 ignore_item 非空即整体失败。
  /// @return false 表示入参不合法或存在非法道具 (此时输出列表只写了已处理的部分)
  ITEM_ALGORITHM_API bool on_find_positions(
      const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
      const item_basic_readable_iterable& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const override;

  // ============================================================
  // 排序键与条目校验
  // ============================================================

  /// @brief 无限格子的排序键 = (行, 列) 组合 (槽位道具为槽位序号)
  ///
  /// 被调用时机: make_entry 建条目时, 以及 set_entry_sort_key 在位置变化后刷新排序键时。
  /// 返回值由条目保存, 容器按它把条目排进同类型的 std::set。
  /// 坐标经虚函数 extract_position 从 proto 位置字段取出 (映射由接入层实现)。
  ITEM_ALGORITHM_API uint64_t make_entry_sort_key(const PROJECT_NAMESPACE_ID::DItemInstance& instance) const override;

  /// @brief 道具字段是否与本模式自洽 (占格道具 + 位置配置存在 + GUID 按配置要求)
  ///
  /// 被调用时机: 基类的 validate_item_basic, 即所有 check_* / load / apply_entries 逐条校验时。
  ITEM_ALGORITHM_API bool is_item_valid(const excel_config_group_ptr_t& config_group,
                                        const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;

  /// @brief 位置字段声明 (接入层的位置字段)
  ///
  /// 库内不再自己映射 proto 位置字段, 这里只保存 init 传入的声明供接入层读取;
  /// 被调用时机: 接入层实现 extract_position / apply_position 时按需要取。
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase get_position_type() const {
    return position_type_;
  }

  /// @brief 无限格子寻位钩子 (子类按自己的槽位规则决定落位)
  ///
  /// 被调用时机: on_find_positions 对每件待放置道具调用一次;
  /// 返回 true 后候选位置还要再通过 on_check_add 才写进 success_item, 否则该件进 failed_item。
  /// 本模式没有格子与占用可推算, 因此不实现"扫描空格"的默认策略。
  /// @param config_group 配置组
  /// @param basic        待放置道具 (含位置 hint)
  /// @param out_pos      [out] 找到的目标位置 (返回 true 时有效)
  /// @return true 表示成功找到位置, false 表示无法放置 (默认实现返回 false)
  ITEM_ALGORITHM_API virtual bool on_find_position_for_infinite(const excel_config_group_ptr_t& config_group,
                                                                const PROJECT_NAMESPACE_ID::DItemBasic& basic,
                                                                PROJECT_NAMESPACE_ID::DItemGridPosition& out_pos) const;

  // ---- 位置与身份索引 (单槽位: 每个位置最多一个条目) ----
  /// @brief 取槽位上的条目
  ///
  /// 被调用时机: 基类 find_entry 使用它, 本模式的 check_add / check_sub / check_move / load /
  /// apply_entries 也用它判断目标槽位是否已被占用。
  /// @return nullptr 表示该槽位空闲
  ITEM_ALGORITHM_API item_entry_ptr_t find_entry_at_position(const ItemGridPosition& position) const override;
  ITEM_ALGORITHM_API void attach_entry_position(const ItemGridPosition& position, const item_entry_ptr_t& entry);
  ITEM_ALGORITHM_API void detach_entry_position(const ItemGridPosition& position);
  ITEM_ALGORITHM_API item_entry_ptr_t find_entry_by_guid(int64_t guid) const override;
  ITEM_ALGORITHM_API bool has_entry_guid(int64_t guid) const override;
  ITEM_ALGORITHM_API void attach_entry_guid(int64_t guid, const item_entry_ptr_t& entry);
  ITEM_ALGORITHM_API void detach_entry_guid(int64_t guid);

  /// @brief 记录条目的槽位索引 / GUID 索引 (本模式每件道具占一个槽位, 与位置配置无关)
  ///
  /// 被调用时机: 条目新建并入分组之后 (add / load / 客户端同步新增)。
  ITEM_ALGORITHM_API void attach_entry_index(const item_entry_ptr_t& entry);
  /// @brief 解除条目的槽位索引 / GUID 索引
  ///
  /// 被调用时机: 条目即将整体移除, 或客户端同步改了位置 / GUID 需要重挂之前。
  ITEM_ALGORITHM_API void detach_entry_index(const item_entry_ptr_t& entry);

  /// @brief 记录条目的位置索引 / GUID 索引 (带位置配置的调用形状, 本模式不用配置)
  ITEM_ALGORITHM_API void add_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                          const item_entry_ptr_t& entry);
  /// @brief 解除条目的位置索引 / GUID 索引 (操作流程调用)
  ITEM_ALGORITHM_API void remove_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                             const item_entry_ptr_t& entry);

  /// @brief 清空本模式自己维护的索引 (槽位索引与 GUID 索引)
  ///
  /// 被调用时机: 基类 clear() 清空条目存储之后调用; init_container 也会走一次 clear()。
  ITEM_ALGORITHM_API void on_clear() override;

 private:
  /// @brief move 的 Phase 0: 校验入参并填充 Helper 字段
  ///
  /// 被调用时机: check_move 在检查空请求之后、推演槽位占用之前调用;
  /// 返回 false 时 checked_request.result.error_code 已经写好, 调用方直接返回。
  bool check_move_request(const excel_config_group_ptr_t& config_group, ItemMoveCheckedRequest& checked_request) const;

 private:
  PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type_ =
      PROJECT_NAMESPACE_ID::DItemGridPosition::POSITION_TYPE_NOT_SET;
  position_index_type position_index_;
  guid_index_type guid_index_;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
