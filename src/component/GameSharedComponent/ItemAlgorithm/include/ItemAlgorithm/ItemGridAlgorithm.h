// Copyright 2025 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>
#include <ItemAlgorithm/ItemAlgorithmLog.h>
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

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251)
#endif

namespace item_algorithm {

enum class ItemGridAlgorithmMode : int32_t {
  kFiniteGrid = 0,    ///< 有限格子模式
  kInfiniteGrid = 1,  ///< 无限格子模式
  kNoPosition = 2,    ///< 无位置模式
};

class ItemGridAlgorithm;
using item_grid_algorithm_ptr_t = atfw::util::memory::strong_rc_ptr<ItemGridAlgorithm>;

/// @brief 背包格子算法虚基类
///
/// 提供二维背包格子管理能力。继承方需要注册:
///   - 格子大小(行列数), 是否关心物品XY大小
///   - apply_position(position, grid_pos): 将运行时坐标写回 protobuf
///
/// 支持三种背包模式:
///   1. 有限大小背包: 行列有限, 物品占据多个格子, 需要检查碰撞和边界
///   2. 无限大小背包: 不关心物品XY大小, 只记录位置, 仅检查位置是否被占用
//    3. 无位置模式:   不关心物品XY大小, 也不关心位置, 仅记录物品列表, 不检查位置是否被占用
class ITEM_ALGORITHM_API ItemGridAlgorithm : public atfw::util::memory::enable_shared_rc_from_this<ItemGridAlgorithm> {
  friend struct ItemGridEntry;

 public:
  using item_group_type = std::set<item_grid_entry_ptr_t, item_grid_entry_ptr_comparator>;
  using item_group_map_type = std::unordered_map<int32_t, item_group_type>;
  using position_index_type =
      std::unordered_map<ItemGridPosition, item_grid_entry_ptr_t, ItemGridPositionHash, ItemGridPositionEqualTo>;
  using guid_index_type = std::unordered_map<int64_t, item_grid_entry_ptr_t>;
  using position_set_type = std::unordered_set<ItemGridPosition, ItemGridPositionHash, ItemGridPositionEqualTo>;

 public:
  ItemGridAlgorithm();
  virtual ~ItemGridAlgorithm();

  ItemGridAlgorithm(const ItemGridAlgorithm&) = delete;
  ItemGridAlgorithm& operator=(const ItemGridAlgorithm&) = delete;
  ItemGridAlgorithm(ItemGridAlgorithm&&) = delete;
  ItemGridAlgorithm& operator=(ItemGridAlgorithm&&) = delete;

 public:
  // 初始化 FiniteGrid 关心row_size, column_size
  virtual void init(ItemGridAlgorithmMode mode, int32_t row_size, int32_t column_size,
                    PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid);

 public:
  // 操作接口
  ItemGridOperationResult add(ItemGridAddCheckedRequest& checked_request,
                              ItemGridOperationReason reason = ItemGridOperationReason::kAdd);
  ItemGridAddCheckedRequest check_add(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      ItemGridAddRequest&& requests) const;

  ItemGridOperationResult sub(ItemGridSubCheckedRequest& checked_request,
                              ItemGridOperationReason reason = ItemGridOperationReason::kSub);
  ItemGridSubCheckedRequest check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      ItemGridSubRequest&& requests) const;

  // 无位置模式 不允许使用move
  ItemGridOperationResult move(ItemGridMoveCheckedRequest& checked_request);
  ItemGridMoveCheckedRequest check_move(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      ItemGridMoveRequest&& request) const;

  // 整体替换: 将 Grid 内所有条目移除后放入新列表 (业务功能, 与 load 初始化并存)
  ItemGridOperationResult replace(ItemGridReplaceCheckedRequest& checked_request);
  ItemGridReplaceCheckedRequest check_replace(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      ItemGridReplaceRequest&& requests) const;

  ItemGridOperationResult check_has(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridHasRequest& requests) const;

 public:
  // 客户端模式同步接口
  void apply_entries(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ::google::protobuf::RepeatedField<uint64_t>& remove_entry_ids,
      const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry>& update_entries);

 public:
  // 初始化后加载接口
  bool load(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
            const PROJECT_NAMESPACE_ID::DItemInstance& item_instance);
  // 清空
  void clear();
  // 转化坐标
  ItemGridPosition extract_position(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const;

 public:
  // 日志接口
  // 注册日志处理器 (未注册 on_log 时 log() 调用会被忽略)
  void set_log_handler(const ItemLogHandler& handler);
  const ItemLogHandler& get_log_handler() const;
  // 输出日志 (未注册处理器时忽略)
  void log(ItemLogLevel level, const char* file_name, int line_number, const std::string& message) const;

 public:
  // 查询接口
  void foreach (std::function<bool(const PROJECT_NAMESPACE_ID::DItemInstance&)> fn) const;
  item_grid_entry_ptr_t get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const;
  item_grid_entry_ptr_t get_by_guid(int64_t guid) const;
  // 按照位置排序了的数据
  const item_group_type* get_group(int32_t type_id) const;
  // 遍历接口 不建议在正常流程中调用
  int64_t get_item_count(int32_t type_id) const;
  int64_t get_cached_item_count(int32_t type_id) const;
  uint64_t peek_next_entry_id() const { return next_entry_id_; }
  bool is_empty() const;
  const item_group_map_type& get_all_groups() const;
  const std::vector<std::vector<bool>>& get_occupy_grid_flag() const;
  int32_t get_row_size() const;
  int32_t get_column_size() const;
  int64_t get_container_guid() const;

  // 通过Basic提取数据
  item_grid_entry_ptr_t find_entry(const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;
  item_grid_entry_ptr_t find_entry_by_id(uint64_t entry_id) const;

  /// @brief 为物品寻找位置 (只读规划, 不修改背包数据)
  ///
  /// @param config_group 配置组
  /// @param basics        待放置的道具列表
  /// @param ignore_item   本次消耗掉、即将从背包移除的道具 (可为空)
  /// @param success_item  [out] 成功找到位置的道具, 每个元素为带位置的 DItemBasic
  ///                      (位置已写入 position, count 为该位置放入的数量; 拆堆时一个
  ///                      basic 可能对应多个元素)
  /// @param failed_item   [out] 未找到位置的道具 (原样传出, count 为剩余未放入数量)。
  ///                      调用方把 failed_item 作为下一次 basics 再次调用 (多背包依次塞入)。
  /// @return true 表示调用成功 (校验与寻位流程正常执行); false 表示整体失败
  ///         (仅当传入参数有问题或配置有问题时返回 false, 如 ignore_item 非法、
  ///         未知道具类型、无位置模式放入占格道具等)。
  ///
  /// 模式约束:
  ///   - 有限格子模式 (is_occupy_flag): 完整支持 ignore_item 与拆堆放置。
  ///   - 无限格子不占格模式 (!is_occupy_flag && !is_ignore_position):
  ///     委托 on_find_position_for_infinite, 不支持 ignore_item, 非空则整体失败。
  ///   - 无位置模式 (is_ignore_position): 只检查能否放入 (道具是否不占格),
  ///     不寻找位置, 不支持 ignore_item, 非空则整体失败。
  ///   - 无位置模式下占格道具无法放入, 整体失败 (外层负责按 Grid 分发, 属调用问题)。
  bool find_positions_for_basics(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed_item) const;

  /// @brief 为物品寻找位置 (只读规划, 不修改背包数据), 输入为 DItemInstance
  ///
  /// 语义与 find_positions_for_basics 一致, 仅输入输出类型不同:
  ///   - items / success_item / failed_item 均为 DItemInstance
  ///   - ignore_item 仍为 DItemBasic (消耗掉、即将从背包移除的道具)
  bool find_positions_for_instances(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& items,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const;

 protected:
  // ============================================================
  // Replace check 辅助 create_empty_clone 子类必须覆盖
  // ============================================================
  virtual item_grid_algorithm_ptr_t create_empty_clone() const;
  void copy_empty_config_to(ItemGridAlgorithm& out) const;

  // ============================================================
  // 子类可覆盖的额外检查钩子 (默认返回 EN_SUCCESS)
  // ============================================================

  /// @brief 根据 type_id 查表获取物品位置配置(当存在额外逻辑时可负载,默认从表内实现)
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
      const PROJECT_NAMESPACE_ID::DItemInstance& request) const;

  /// @brief 子类对单个扣减请求的额外检查
  virtual int32_t on_check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const PROJECT_NAMESPACE_ID::DItemBasic& request) const;

  /// @brief 道具数量上限检查钩子
  virtual int32_t on_check_item_count_limit(int32_t type_id, int64_t current_count, int64_t add_count) const;

  /// @brief 无限格子寻位钩子 (有限格子 find_positions_for_basics 调用)
  ///
  /// 子类覆盖此函数以实现装备槽等无限格子模式的自动寻位逻辑。
  /// 默认实现返回 false（无法自动确定位置）。
  ///
  /// @param config_group 配置组
  /// @param basic         待放置道具 (含首选位置 hint)
  /// @param out_pos       [out] 找到的目标位置 (返回 true 时有效)
  /// @return true 表示成功找到位置, false 表示无法放置
  virtual bool on_find_position_for_infinite(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const PROJECT_NAMESPACE_ID::DItemBasic& basic, PROJECT_NAMESPACE_ID::DItemGridPosition& out_pos) const;

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

  /// @brief 检查道具额外字段是否合法 container_guid 底层已经判断
  /// @param position 道具位置 用于检查额外字段
  virtual bool check_item_position(const PROJECT_NAMESPACE_ID::DItemPosition& position) const;

  // 物品不足时返回错误码
  // 默认返回 PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH
  virtual int32_t on_item_not_enough(int32_t type_id) const;

 private:
  item_grid_entry_ptr_t make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance);
  item_grid_entry_ptr_t make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance, uint64_t entry_id);

  bool is_occupy_flag() const;
  bool is_ignore_position() const;

  bool check_move_request(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                          ItemGridMoveCheckedRequest& checked_request) const;
  void apply_position(PROJECT_NAMESPACE_ID::DItemGridPosition& position, const ItemGridPosition& grid_pos) const;

  // 寻位公共实现 (模板, 支持 DItemBasic / DItemInstance 两种输入)
  template <typename ItemT>
  bool find_positions_inner(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const google::protobuf::RepeatedPtrField<ItemT>& items,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
      google::protobuf::RepeatedPtrField<ItemT>& success_item,
      google::protobuf::RepeatedPtrField<ItemT>& failed_item) const;

 private:
  bool is_item_valid(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                     const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;
  bool is_item_in_range(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const;
  bool check_collision(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const;
  void set_grid_flag(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size, bool occupied);
  void remove_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                          const item_grid_entry_ptr_t& entry);
  void remove_entry_from_group(const item_grid_entry_ptr_t& entry);
  void add_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg, const item_grid_entry_ptr_t& entry);

  // 不应该再新增以下两个接口的调用处
  void add_entry_id_index(const item_grid_entry_ptr_t& entry);
  void remove_entry_id_index(uint64_t entry_id);

 private:
  bool init_ = false;
  ItemGridAlgorithmMode mode_;
  int64_t container_guid_ = 0;
  int32_t row_size_ = 0;
  int32_t column_size_ = 0;
  PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type_;

  std::unordered_map<uint64_t, item_grid_entry_weak_ptr_t> entry_id_index_;
  item_group_map_type item_groups_;
  position_index_type position_index_;
  guid_index_type guid_index_;
  std::vector<std::vector<bool>> occupy_grid_flag_;
  std::unordered_map<int32_t, int64_t> item_count_cache_;
  ItemLogHandler log_handler_;
  mutable uint64_t next_entry_id_ = 1;
  mutable int64_t operate_id_ = 0;  // 操作流水号
};

}  // namespace item_algorithm

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

ITEM_ALGORITHM_NAMESPACE_END
