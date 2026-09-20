// Copyright 2025 atframework

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"
#include "ItemAlgorithm/ItemContainer.h"
#include "ItemAlgorithm/ItemGridOccupancy.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief 批次内位置预留 (有限格子专用)
///
/// check_add / check_move 需要在整个批次内做"位置是否被前面请求占用"的判断,
/// 因此复制一份位图, 在该副本上检查与占用, 不污染容器自身的位图。
class ATFW_UTIL_SYMBOL_VISIBLE ItemPlacementReservation {
 public:
  ITEM_ALGORITHM_API ItemPlacementReservation();
  ITEM_ALGORITHM_API virtual ~ItemPlacementReservation();

  ItemPlacementReservation(const ItemPlacementReservation&) = delete;
  ItemPlacementReservation& operator=(const ItemPlacementReservation&) = delete;
  ITEM_ALGORITHM_API ItemPlacementReservation(ItemPlacementReservation&&) noexcept;
  ITEM_ALGORITHM_API ItemPlacementReservation& operator=(ItemPlacementReservation&&) noexcept;
};

using item_placement_reservation_ptr_t = std::unique_ptr<ItemPlacementReservation>;

/// @brief 有限格子容器
///
/// 行列数量固定, 道具按配置占用 1..N 个格子, 需要边界检查与矩形碰撞检查。
/// 位置占用同时维护"锚点位置索引 + 位图"两份数据:
///   - 锚点索引用于按位置直接取条目
///   - 位图用于矩形区域的快速占用判断, 以及 find_positions 的空位搜索
///
/// 本类只提供有限格子自己的行为与数据结构: 批次检查钩子 (on_check_add / on_check_sub /
/// on_check_has)、单条执行钩子 (on_add_one / on_sub_one / on_load_one / on_apply_remove_one /
/// on_apply_update_one / on_find_positions)、格子与占用判断、位置与 GUID 索引, 以及没有批次语义的
/// move / check_move。增删改查的批次流程 (check_add / add / sub / check_has / load /
/// apply_entries / find_positions_*) 全部在基类 ItemContainer 里实现; 基类完成通用校验后一次调用
/// 本模式的批次检查钩子, 单条执行钩子在 ItemFiniteGridContainerOperation.cpp, 寻位在
/// ItemFiniteGridFindPosition.cpp。
///
/// 库内不做 proto 位置字段的映射: 位置字段由接入层读取 (extract_position / apply_position),
/// 本类推不出最终容器类型。
class ATFW_UTIL_SYMBOL_VISIBLE ItemFiniteGridContainer : public ItemContainer {
 public:
  using position_index_type =
      std::unordered_map<ItemGridPosition, item_entry_ptr_t, ItemGridPositionHash, ItemGridPositionEqualTo>;
  using guid_index_type = std::unordered_map<int64_t, item_entry_ptr_t>;
  using position_set_type = std::unordered_set<ItemGridPosition, ItemGridPositionHash, ItemGridPositionEqualTo>;

 protected:
  /// @brief 一次批次校验期间本模式自己的状态 (三个批次检查钩子各建一份)
  ///
  /// 每个批次检查钩子在开始时创建自己的状态, 仅本模式解释其内容。
  /// 任一条校验失败就整批失败, 对象随之丢弃, 因此不需要回滚。
  class ItemFiniteGridCheckScratch {
   public:
    ItemFiniteGridCheckScratch() = default;
    ~ItemFiniteGridCheckScratch() = default;

    /// @brief 批次内的格子占用副本 (初值 = 容器当前位图)
    ///
    /// check_add 在它上面推演"本批次前面请求已经占掉的区域", 不污染容器自身的位图。
    ItemGridOccupancy reserved_occupancy;

    /// @brief 锚点位置 -> 本批次已在该锚点堆叠的额外数量 (check_add)
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_existing_extra;

    /// @brief 本批次新建锚点的待放入数量 (check_add)
    struct PendingNewSlot {
      int32_t type_id = 0;
      int64_t accumulated_count = 0;
    };
    std::unordered_map<ItemGridPosition, PendingNewSlot, ItemGridPositionHash, ItemGridPositionEqualTo>
        pending_new_slots;

    /// @brief GUID -> 本批次已扣减 / 已对账的数量 (check_sub / check_has)
    std::unordered_map<int64_t, int64_t> pending_guid_count;

    /// @brief 锚点位置 -> 本批次已扣减 / 已对账的数量 (check_sub / check_has)
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_position_count;
  };

 public:
  ITEM_ALGORITHM_API ItemFiniteGridContainer();
  ITEM_ALGORITHM_API ~ItemFiniteGridContainer() override;

  ItemFiniteGridContainer(const ItemFiniteGridContainer&) = delete;
  ItemFiniteGridContainer& operator=(const ItemFiniteGridContainer&) = delete;
  ItemFiniteGridContainer(ItemFiniteGridContainer&&) = delete;
  ItemFiniteGridContainer& operator=(ItemFiniteGridContainer&&) = delete;

 public:
  /// @brief 初始化有限格子容器
  /// @param row_size      行数 (<=0 时按 1 处理)
  /// @param column_size   列数 (<=0 时按 1 处理)
  /// @param position_type 容器对应的位置字段
  /// @param container_guid 容器唯一 ID
  ITEM_ALGORITHM_API void init(int32_t row_size, int32_t column_size,
                               PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                               int64_t container_guid);

  // ---- 移动 ----
  // check_move / move 的流程 (入参自持 / 初始化检查 / 空请求 / 两阶段循环 / 结果汇总) 在基类;
  // 本模式只实现移动语义钩子: on_check_move (校验来源与目标、预演批次内占用) 与
  // on_move_sub_one / on_move_add_one (扣来源、放目标)。


 public:
  // ---- 格子与位置查询 ----
  ITEM_ALGORITHM_API int32_t get_row_size() const;
  ITEM_ALGORITHM_API int32_t get_column_size() const;
  ITEM_ALGORITHM_API const ItemGridOccupancy& get_occupy_grid_flag() const;
  /// @brief 取锚点位置上的条目
  ITEM_ALGORITHM_API item_entry_ptr_t get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const;
  /// @brief 按 GUID 取条目
  ITEM_ALGORITHM_API item_entry_ptr_t get_by_guid(int64_t guid) const;

  /// @brief 查询与指定矩形区域相交的所有条目
  ///
  /// @param config_group 配置组
  /// @param x            起始列
  /// @param y            起始行
  /// @param row_size     区域行数
  /// @param column_size  区域列数
  /// @param out_entries  [out] 相交的条目, 按 (行, 列, entry_id) 排序; 每次调用先清空
  ///
  /// 返回锚点落在区域内, 或占格矩形与区域有重叠的条目。与 get(position) 不同,
  /// 这里能查到"覆盖了该区域但锚点在区域外"的多格道具。不占格道具没有区域, 不计入;
  /// 区域尺寸 <= 0 时结果为空。
  ITEM_ALGORITHM_API void get_entries_in_area(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group, int32_t x, int32_t y,
      int32_t row_size, int32_t column_size, std::vector<item_entry_ptr_t>& out_entries) const;

 protected:
  /// @brief 接入层的位置字段声明
  ///
  /// 库内不再自己映射位置字段, 只保存 init 传入的值, 供接入层的 extract_position / apply_position 读取。
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase get_position_type() const {
    return position_type_;
  }

  // ============================================================
  // 排序键与格子判断
  // ============================================================

  /// @brief 算条目的排序键, 有限格子取 (行, 列) 组合 (与位置字段解析方式一致)
  ///
  /// 被调用时机: 基类 make_entry 建条目时, 以及 apply_entries 改完条目位置后由 set_entry_sort_key 调用;
  /// 返回值被条目当作不透明键保存, insert_entry_into_group 按它排序。子类不需要覆盖。
  ITEM_ALGORITHM_API uint64_t make_entry_sort_key(const PROJECT_NAMESPACE_ID::DItemInstance& instance) const override;

  /// @brief 有限格子: 行列边界 + 尺寸必须为正
  ///
  /// 被调用时机: on_check_add 逐条校验与 check_move 在按位置放置之前调用;
  /// 返回 false 表示该矩形不落在行列范围内。
  ITEM_ALGORITHM_API bool is_item_in_range(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const;
  /// @brief 有限格子: 逐格查位图
  ///
  /// 被调用时机: on_load_one 新建条目之前调用; 返回 true 表示矩形越界或与已占格子重叠。
  ITEM_ALGORITHM_API bool check_collision(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const;

  // ============================================================
  // 条目校验 (本模式只接受占格道具, 且必须有位置配置)
  // ============================================================

  /// @brief 道具字段是否与本模式自洽 (占格道具 + 位置配置存在 + GUID 按配置要求)
  ///
  /// 被调用时机: 基类的批次校验 (check_add / check_sub / check_has / load / apply_entries) 与寻位
  /// 在每条请求上调用; 返回 false 的请求一律按失败处理。
  ITEM_ALGORITHM_API bool is_item_valid(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const PROJECT_NAMESPACE_ID::DItemBasic& basic) const override;

  /// @brief 寻位引擎 (ItemFiniteGridFindPositionHelper) 按本模式的位图语义推演空位
  friend class ItemFiniteGridFindPositionHelper;

  // ============================================================
  // 批次检查钩子 — 基类已经完成通用字段校验与按类型的数量校验
  // ============================================================

  /// @brief 一批放入请求的本模式检查 (位置配置 / 堆叠 / 锚点与矩形占用推演)
  ///
  /// 被调用时机: 基类 check_add 完成所有通用字段校验后调用一次。
  /// 本模式在此按请求顺序检查格子占用与同批次堆叠数量。
  ITEM_ALGORITHM_API ItemOperationResult on_check_add(const ItemAddCheckedRequest& checked_request) const override;

  /// @brief 一批扣减请求的本模式检查 (带 GUID 按 GUID 定位, 其余按锚点位置定位)
  ///
  /// 被调用时机: 基类 check_sub 完成所有通用字段与按 type_id 总量校验后调用一次。
  /// 本模式在此按请求顺序累计同一 GUID / 锚点的扣减数量。
  ITEM_ALGORITHM_API ItemOperationResult on_check_sub(const ItemSubCheckedRequest& checked_request) const override;

  /// @brief 一批对账请求的本模式检查 (按 GUID 或锚点位置定位并核对数量)
  ///
  /// 被调用时机: 基类 check_has 完成所有通用字段与按 type_id 总量校验后调用一次。
  /// 本模式在此按请求顺序累计同一 GUID / 锚点的对账数量。
  ITEM_ALGORITHM_API ItemOperationResult on_check_has(const excel_config_group_ptr_t& config_group,
                                                      const item_basic_readable_iterable& requests) const override;

  // ============================================================
  // 单条执行钩子 — 基类负责 checked request 校验与循环, 这里只处理一件
  // ============================================================

  /// @brief 放入一件道具 (合入锚点上的已有条目, 或新建条目并挂上位置索引与位图占用)
  ///
  /// 被调用时机: 基类 add 逐条循环, checked request 已校验通过; load 也走这里。
  /// 本条不合法 (类型缺失 / 不占格 / 位置配置缺失) 或条目没能进分组时只记日志并返回成功,
  /// 让整批继续 (这些分支都没动数量缓存, 与旧实现的 continue 一致)。
  /// @param instance 本条待放入的道具 (只读)
  /// @param context 钩子上下文 (容器判定的原因 + 调用方透传的来源)
  ITEM_ALGORITHM_API ItemOperationResult on_add_one(ItemAddCheckedRequest& checked_request,
                                                    const PROJECT_NAMESPACE_ID::DItemInstance& instance,
                                                    const ItemOperationContext& context) override;

  /// @brief 扣减一件道具 (扣完为 0 就整体移除条目, 摘掉位置索引、位图占用与 GUID 索引)
  ///
  /// 被调用时机: 基类 sub 逐条循环, checked request 已校验通过。
  /// 条目定位不到或位置配置缺失时只记日志并返回成功, 让整批继续。
  /// @param request 本条待扣减的请求 (只读)
  ITEM_ALGORITHM_API ItemOperationResult on_sub_one(ItemSubCheckedRequest& checked_request,
                                                   const PROJECT_NAMESPACE_ID::DItemBasic& request,
                                                   const ItemOperationContext& context) override;

  /// @brief 移动请求的本模式校验 (来源 / 目标条目、目标范围、批次内格子占用)
  ///
  /// 被调用时机: 基类 check_move 完成初始化检查与空请求判断后调用一次。
  /// 失败时把错误码写进 checked_request.result.error_code 并返回 false。
  ITEM_ALGORITHM_API bool on_check_move(ItemMoveCheckedRequest& checked_request) const override;

  /// @brief 扣减一条移动来源 (整体移走时摘掉位置索引 / 位图占用 / GUID 索引)
  ///
  /// 被调用时机: 基类 move 的 kMoveSub 阶段逐条循环, 每条一次。
  ITEM_ALGORITHM_API ItemOperationResult on_move_sub_one(ItemMoveCheckedRequest& checked_request,
                                                        const ItemMoveSubRequest& request,
                                                        const ItemOperationContext& context) override;

  /// @brief 放入一条移动目标 (目标锚点已有同类无 GUID 条目时合入, 否则新建条目并挂索引)
  ///
  /// 被调用时机: 基类 move 的 kMoveAdd 阶段逐条循环 (全部 kMoveSub 之后)。
  /// 位置配置缺失或条目没能进分组时只记日志并返回成功 (没有可回滚的数据)。
  ITEM_ALGORITHM_API ItemOperationResult on_move_add_one(ItemMoveCheckedRequest& checked_request,
                                                        const ItemMoveAddRequest& request,
                                                        const ItemOperationContext& context) override;


  /// @brief 载入一条持久化数据 (同锚点同类无 GUID 条目合并, 否则新建并要求位置合法且不碰撞)
  ///
  /// 被调用时机: 基类 load 通过通用校验 (条目字段 / 容器归属 / 位置字段 / 类型存在 / GUID 唯一 /
  /// 数量上限) 之后调用一次。载入的是已经存在的存档数据, 不做堆叠上限检查;
  /// 锚点被占用、矩形越界或与已占格子碰撞都返回 false。
  ITEM_ALGORITHM_API bool on_load_one(const excel_config_group_ptr_t& config_group,
                                      const PROJECT_NAMESPACE_ID::DItemInstance& item_instance,
                                      const ItemOperationContext& context) override;

  /// @brief 应用客户端同步的"删除一条" (摘索引 / 占位 / 移出分组 / 通知数量变化)
  ///
  /// 被调用时机: 基类 apply_entries 的删除阶段, 每条已经找到的条目调用一次。
  ITEM_ALGORITHM_API void on_apply_remove_one(const excel_config_group_ptr_t& config_group,
                                              const item_entry_ptr_t& entry,
                                              const ItemOperationContext& context) override;

  /// @brief 应用客户端同步的"新增或更新一条"
  ///
  /// 被调用时机: 基类 apply_entries 的新增/更新阶段, 每条已通过通用校验之后调用一次。
  /// 已有条目位置变化时重新排序 (先摘出分组, 刷排序键, 再插回) 并按新锚点重挂索引与占位;
  /// 位置没变但 GUID 变了只需要重挂 GUID 索引; 条目不存在则按同步包的 entry_id 新建并挂索引。
  /// @param config_group 配置组 (查位置配置以挂摘位图与位置索引)
  /// @param existing 该 entry_id 已存在的条目 (不存在时为 nullptr)
  ITEM_ALGORITHM_API void on_apply_update_one(const excel_config_group_ptr_t& config_group,
                                              const item_entry_ptr_t& existing,
                                              const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update,
                                              const ItemOperationContext& context) override;

  // ============================================================
  // 寻位钩子
  // ============================================================

  /// @brief 为一批道具寻位的本模式实现 (位图扫描空格 + 优先堆叠到已有条目)
  ///
  /// 被调用时机: 基类 find_positions_for_instances (及由它实现的 find_positions_for_basics) 在
  /// 初始化检查与输出预留之后调用一次。
  /// @param ignore_item  本次消耗掉、即将从容器移除的道具 (用来把这些道具占的格子当成空闲)
  /// @param success_item [out] 成功找到位置的道具 (位置字段与容器归属已写回)
  /// @param failed_item  [out] 放不下的道具 (进入时清空)
  /// @return false 表示整体失败 (传参不合法, 或某条待放置 / 待消耗道具不合法)
  ITEM_ALGORITHM_API bool on_find_positions(
      const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
      const item_basic_readable_iterable& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const override;

  // ---- 位置预留与位图占用 (有限格子专用) ----

  /// @brief 复制一份本容器位图, 供批次内推演占用 (check_add / check_move 开头调用)
  ITEM_ALGORITHM_API item_placement_reservation_ptr_t create_placement_reservation() const;
  /// @brief 检查 (x, y) 起的矩形是否被占用, 并把它置为已占用
  ///
  /// 被调用时机: check_add / check_move 在临时位图副本上检查候选位置; 返回 true 表示已被本批次占掉。
  ITEM_ALGORITHM_API bool is_occupancy_blocked(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size,
                                               ItemPlacementReservation& reservation) const;
  /// @brief 在临时位图副本上释放 (x, y) 起的矩形
  ///
  /// 被调用时机: check_move 处理整体移走的来源条目时调用, 使目标位置可以落回原处。
  ITEM_ALGORITHM_API void release_reserved_position(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size,
                                                    ItemPlacementReservation& reservation) const;
  /// @brief 按 position_cfg 的行列尺寸, 把整个矩形区域的占位标志置为 occupied
  ///
  /// 被调用时机: add_entry_index / remove_entry_index 在挂摘条目索引时调用 (条目新增、整体移除、位置变更)。
  ITEM_ALGORITHM_API void on_update_grid_occupancy(const ItemGridPosition& position,
                                                   const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                                   bool occupied);

  // ---- 位置与身份索引 (有限格子按矩形区域维护位图, 锚点位置 + GUID 各一份索引) ----
  /// @brief 取锚点位置上的条目
  ///
  /// 被调用时机: add / check_add / check_sub / check_has / check_move / load / apply_entries / 寻位在判断
  /// 位置占用或定位已有条目时调用; 找不到返回 nullptr。
  ITEM_ALGORITHM_API item_entry_ptr_t find_entry_at_position(const ItemGridPosition& position) const override;
  ITEM_ALGORITHM_API void attach_entry_position(const ItemGridPosition& position, const item_entry_ptr_t& entry);
  ITEM_ALGORITHM_API void detach_entry_position(const ItemGridPosition& position);
  /// @brief 按 GUID 取条目
  ///
  /// 被调用时机: check_sub / check_has / check_move / load / apply_entries / 寻位按 GUID 对账时调用;
  /// 找不到返回 nullptr。
  ITEM_ALGORITHM_API item_entry_ptr_t find_entry_by_guid(int64_t guid) const override;
  ITEM_ALGORITHM_API bool has_entry_guid(int64_t guid) const;
  ITEM_ALGORITHM_API void attach_entry_guid(int64_t guid, const item_entry_ptr_t& entry);
  ITEM_ALGORITHM_API void detach_entry_guid(int64_t guid);

  /// @brief 记录条目的位置索引 / 位图占用 / GUID 索引 (操作流程调用)
  ///
  /// 被调用时机: add / move / load / apply_entries 在条目进入容器之后调用; 不占格道具不会写入索引。
  ITEM_ALGORITHM_API void add_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                          const item_entry_ptr_t& entry);
  /// @brief 解除条目的位置索引 / 位图占用 / GUID 索引 (操作流程调用)
  ///
  /// 被调用时机: sub / move / apply_entries 在条目整体移出容器之前调用。
  ITEM_ALGORITHM_API void remove_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                             const item_entry_ptr_t& entry);

  /// @brief 清空本模式自己维护的索引
  ///
  /// 被调用时机: 基类 clear() 清完条目存储之后调用 (init_container 也会走到); 必须把位置索引 / GUID
  /// 索引 / 占用位图一起清掉, 否则清空后索引会残留已删除条目。
  ITEM_ALGORITHM_API void on_clear() override;

 private:
  /// @brief move 的 Phase 0: 校验入参并填充 Helper 字段
  bool check_move_request(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                          ItemMoveCheckedRequest& checked_request) const;

 private:
  int32_t row_size_ = 0;
  int32_t column_size_ = 0;
  PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type_ =
      PROJECT_NAMESPACE_ID::DItemGridPosition::POSITION_TYPE_NOT_SET;
  ItemGridOccupancy occupy_grid_flag_;
  position_index_type position_index_;
  guid_index_type guid_index_;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
