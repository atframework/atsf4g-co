// Copyright 2025 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>
#include <ItemAlgorithm/ItemGridData.h>

#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace excel {
struct config_group_t;
}

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief 背包格子算法虚基类
///
/// 提供二维背包格子管理能力。继承方需要注册:
///   - 格子大小(行列数), 是否关心物品XY大小
///   - apply_position(position, grid_pos): 将运行时坐标写回 protobuf
///
/// 支持两种背包模式:
///   1. 有限大小背包: 行列有限, 物品占据多个格子, 需要检查碰撞和边界
///   2. 无限大小背包: 不关心物品XY大小, 只记录位置, 仅检查位置是否被占用
class ITEM_ALGORITHM_API ItemGridAlgorithm {
 public:
  /// 同type_id下的物品列表
  using item_group_type = std::list<item_grid_entry_ptr_t>;
  /// type_id -> item_group 映射
  using item_group_map_type = std::unordered_map<int32_t, item_group_type>;
  /// ItemGridPosition -> entry 映射
  using position_index_type =
      std::unordered_map<ItemGridPosition, item_grid_entry_ptr_t, ItemGridPositionHash, ItemGridPositionEqualTo>;
  /// guid -> entry 映射
  using guid_index_type = std::unordered_map<int64_t, item_grid_entry_ptr_t>;
  /// 临时位置集合(用于check系列接口避免拷贝)
  using position_set_type = std::unordered_set<ItemGridPosition, ItemGridPositionHash, ItemGridPositionEqualTo>;

 public:
  ItemGridAlgorithm();
  virtual ~ItemGridAlgorithm();

  ItemGridAlgorithm(const ItemGridAlgorithm&) = delete;
  ItemGridAlgorithm& operator=(const ItemGridAlgorithm&) = delete;
  ItemGridAlgorithm(ItemGridAlgorithm&&) = delete;
  ItemGridAlgorithm& operator=(ItemGridAlgorithm&&) = delete;

  // ============================================================
  // 初始化/重置
  // ============================================================

  /// @brief 初始化背包, 设置行列大小并清空数据, 背包类型
  void init(int32_t row_size, int32_t column_size,
            PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type);

  // ============================================================
  // 核心操作接口
  // ============================================================

  /// @brief 添加物品(批量)
  /// @param checked_request 由 check_add 返回的已校验请求包装
  /// @return 操作结果
  ItemGridOperationResult add(const ItemGridAddCheckedRequest& checked_request);

  /// @brief 检查是否可以添加物品(批量), 不实际修改数据
  /// @param requests 添加请求数组
  /// @return 已校验请求包装(含 config_group / requests / result)
  ItemGridAddCheckedRequest check_add(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const std::vector<ItemGridAddRequest>& requests) const;

  /// @brief 扣减物品(批量)
  /// @param checked_request 由 check_sub 返回的已校验请求包装
  /// @return 操作结果
  ItemGridOperationResult sub(const ItemGridSubCheckedRequest& checked_request);

  /// @brief 检查是否可以扣减物品(批量), 不实际修改数据
  /// @param requests 扣减请求数组
  /// @return 已校验请求包装(含 config_group / requests / result)
  ItemGridSubCheckedRequest check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const std::vector<ItemGridSubRequest>& requests) const;

  /// @brief 移动物品(批量)
  /// @param checked_request 由 check_move 返回的已校验请求包装
  /// @return 操作结果
  ItemGridOperationResult move(const ItemGridMoveCheckedRequest& checked_request);

  /// @brief 检查是否可以移动物品, 不实际修改数据
  /// @param request 移动请求 (包含 sub_entrys + add_entrys)
  /// @return 已校验请求包装(含 config_group / request / error_code)
  ItemGridMoveCheckedRequest check_move(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      ItemGridMoveRequest& request) const;

  // ============================================================
  // Load / Foreach 数据接口
  // ============================================================
  bool load(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
            const PROJECT_NAMESPACE_ID::DItemInstance& item_instance);
  /// @brief 返回 false 终止
  void foreach (std::function<bool(const PROJECT_NAMESPACE_ID::DItemInstance&)> fn) const;

  void apply_entries(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ::google::protobuf::RepeatedField<uint64_t>& remove_entry_ids,
      const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry>& update_entries);

  /// @brief 清空所有物品
  void clear();

  item_grid_entry_ptr_t get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const;

  /// @brief 根据GUID获取物品
  /// @param guid 物品GUID
  /// @return 物品指针, 不存在返回空
  item_grid_entry_ptr_t get_by_guid(int64_t guid) const;

  /// @brief 根据type_id获取物品组
  /// @param type_id 物品类型ID
  /// @return 物品组指针, 不存在返回nullptr
  const item_group_type* get_group(int32_t type_id) const;

  /// @brief 获取指定type_id的物品总数量
  /// @param type_id 物品类型ID
  /// @return 数量
  int64_t get_item_count(int32_t type_id) const;
  /// @brief 获取侧向 — 返回下一个将被分配的 entry_id
  /// @note 每个 Grid 独立计数, 从 1 开始自增, 可用于测试验证自增行为
  uint64_t peek_next_entry_id() const { return next_entry_id_; }
  /// @brief 判断背包是否为空
  bool is_empty() const;

  /// @brief 获取所有物品组
  const item_group_map_type& get_all_groups() const;

  /// @brief 获取格子占用标记 (行×列, 仅 care_item_size 模式下有意义)
  const std::vector<std::vector<bool>>& get_occupy_grid_flag() const;

  // ============================================================
  // 背包属性查询
  // ============================================================

  int32_t get_row_size() const;
  int32_t get_column_size() const;

  // ============================================================
  // 位置转换辅助(public)
  // ============================================================

  ItemGridPosition extract_position(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const;
  void apply_position(PROJECT_NAMESPACE_ID::DItemGridPosition& position, const ItemGridPosition& grid_pos) const;

  bool find_positions_for_basics(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
      std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition>& out_positions) const;

  /// @brief 查找物品(通过GUID或position), Container 层跨Grid操作时使用
  item_grid_entry_ptr_t find_entry(const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;

 protected:
  // ============================================================
  // 子类可覆盖的额外检查钩子 (默认返回 EN_SUCCESS)
  // ============================================================

  /// @brief 根据 type_id 查表获取物品位置配置 (子类可覆盖, 用于脱离配置表的单元测试)
  /// @param config_group 配置组
  /// @param basic 物品基础信息
  /// @return 位置配置指针, 不存在返回 nullptr
  virtual const PROJECT_NAMESPACE_ID::DItemPositionCfg* get_item_position_cfg(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;

  /// @brief 子类对单个添加请求的额外检查
  /// @return EN_SUCCESS 表示通过, 其他错误码表示检查失败
  virtual int32_t on_check_add(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridAddRequest& request) const;

  /// @brief 子类对单个扣减请求的额外检查
  virtual int32_t on_check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridSubRequest& request) const;

  /// @brief 道具数量上限检查钩子
  virtual int32_t on_check_item_count_limit(int32_t type_id, int64_t current_count, int64_t add_count) const;

  /// @brief 非格子寻位钩子 (is_care_item_size=false 时由 find_positions_for_basics 调用)
  ///
  /// 子类覆盖此函数以实现装备槽等无格子尺寸模式的自动寻位逻辑。
  /// 默认实现返回 false（无法自动确定位置）。
  ///
  /// @param config_group 配置组
  /// @param basic         待放置道具 (含首选位置 hint)
  /// @param out_pos       [out] 找到的目标位置 (返回 true 时有效)
  /// @return true 表示成功找到位置, false 表示无法放置
  virtual bool on_find_position_for_non_care(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const PROJECT_NAMESPACE_ID::DItemBasic& basic,
      PROJECT_NAMESPACE_ID::DItemGridPosition& out_pos) const;

  // ============================================================
  // 子类可覆盖的变更通知钩子 (默认空实现)
  // ============================================================

  /// @brief 道具数量变化钩子
  /// @param type_id    道具类型 ID
  /// @param entry      变化的道具条目 (完全移除后条目对象仍有效, 但已不在索引中)
  /// @param guid       道具 GUID (无 GUID 时为 0)
  /// @param position   道具位置 (不占格道具为默认值 {0,0})
  /// @param old_count  该条目变化前的数量 (新建时为 0)
  /// @param new_count  该条目变化后的数量 (完全移除时为 0)
  /// @param type_total_count 该类型道具的当前总数量 (缓存值, 已包含本次变更)
  /// @param reason     操作原因
  virtual void on_item_count_changed(int32_t type_id, const item_grid_entry_ptr_t& entry, int64_t guid,
                                     const ItemGridPosition& position, int64_t old_count, int64_t new_count,
                                     int64_t type_total_count, ItemGridOperationReason reason);

  /// @brief 道具数据变化钩子 (数量 / 位置等任意字段变化时触发)
  /// @param entry  数据发生变化的道具条目
  /// @param reason 操作原因
  virtual void on_item_data_changed(const item_grid_entry_ptr_t& entry, ItemGridOperationReason reason);

  /// @brief 获取缓存的类型总数量 (仅供子类在钩子中使用)
  int64_t get_cached_item_count(int32_t type_id) const;

 private:
  item_grid_entry_ptr_t make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance) const;

  bool is_care_item_size() const { return is_care_item_size_; }

  bool check_move_request(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                          ItemGridMoveRequest& request, ItemGridMoveCheckedRequest& checked_request) const;

 private:
  // ============================================================
  // 内部实现
  // ============================================================

  bool is_item_valid(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                     const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;

  bool is_grid_position_valid(const ItemGridPosition& position) const;

  /// @brief 检查物品在指定位置是否会超出背包范围
  bool is_item_in_range(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const;

  /// @brief 检查物品在指定位置是否与已有物品碰撞(对于关心XY大小的背包)
  /// @return true: 有碰撞; false: 无碰撞
  bool check_collision(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const;

  /// @brief 设置格子占用标记
  void set_grid_flag(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size, bool occupied);

  /// @brief 移除 entry 的所有索引
  void remove_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                          const item_grid_entry_ptr_t& entry);

  /// @brief 从 item_groups_ 中移除 entry
  void remove_entry_from_group(const item_grid_entry_ptr_t& entry);

  /// @brief 添加 entry 的所有索引
  void add_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg, const item_grid_entry_ptr_t& entry);

 private:
  int32_t row_size_ = 0;
  int32_t column_size_ = 0;
  bool is_care_item_size_ = true;  // 是否关心物品占格, 影响算法逻辑
  PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type_;

  /// type_id 分组
  item_group_map_type item_groups_;

  /// 位置索引 (ItemGridPosition -> entry)
  position_index_type position_index_;

  /// GUID索引
  guid_index_type guid_index_;

  /// 二维占用标记 [row][column], 仅在 care_item_size 模式下使用
  std::vector<std::vector<bool>> occupy_grid_flag_;

  /// 类型数量缓存 type_id -> total_count (由 add/sub/move 自动维护)
  std::unordered_map<int32_t, int64_t> item_count_cache_;

  /// 每次 make_entry 时自增, 给新建 entry 赋予唯一 ID (per-Grid 独立, 从 1 开始)
  mutable uint64_t next_entry_id_ = 1;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
