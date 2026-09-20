// Copyright 2026 atframework

#pragma once

#include <ItemAlgorithm/Container/ItemFiniteGridContainer.h>
#include <ItemAlgorithm/Container/ItemInfiniteGridContainer.h>
#include <ItemAlgorithm/Container/ItemNoPositionContainer.h>
#include <ItemAlgorithm/ItemContainer.h>
#include <ItemAlgorithm/ItemContainerTypes.h>
#include <memory/rc_ptr.h>

#include <string>
#include <utility>

class user;

namespace item_algorithm = ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm;

/// 负责: 持有 owner / 注册日志处理器 / 把条目变化转发给 user_item_container_manager /
/// 提供同业务类型的空克隆 (check_replace 复用 check_add 校验) / 销毁时注销登记。
/// 批量操作 (check_add / add / check_sub / sub / check_move / move / check_replace / replace /
/// check_has) 直接用 SDK 容器基类 ItemContainer 与组 ItemContainerGroup 的流程, 接入层只负责
/// 位置字段映射 (extract_position / apply_position) 与条目变化通知。
/// 格子语义由 ModeContainerT 提供 (三种模式容器之一); DerivedT 是最终业务类型,
/// 空克隆按它创建, 因此派生类 (如虚拟仓库) 不需要自己再写 create_empty_clone。
template <typename ModeContainerT, typename DerivedT>
class user_item_container : public ModeContainerT {
 public:
  using mode_container_type = ModeContainerT;

  user_item_container(user* owner, const std::string& log_category_prefix);
  ~user_item_container() override;

  user* get_owner() { return owner_; }
  user* get_owner() const { return const_cast<user*>(owner_); }

  /// @brief 模式 init 之后调用: 补全日志分类, 并登记到 user_item_container_manager
  void finish_init();
  /// @brief 注销登记 (容器不再使用时调用)
  void destroy();

 protected:
  item_algorithm::item_container_ptr_t create_empty_clone() const override;

  /// @brief proto 位置字段 → 通用坐标 (接入层的字段映射)
  ///
  /// 组件不映射 proto 位置字段, 这里按 init 声明的位置字段读写:
  /// user_inventory / character_inventory 用 (x, y), character_equipment 用 slot_idx 当 x,
  /// virtual_inventory 只标记归属、不落位。
  item_algorithm::ItemGridPosition extract_position(
      const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const override;

  /// @brief 通用坐标 → proto 位置字段 (按 init 声明的位置字段写回)
  void apply_position(PROJECT_NAMESPACE_ID::DItemGridPosition& position,
                      const item_algorithm::ItemGridPosition& container_pos) const override;

  void on_item_data_changed(const item_algorithm::item_entry_ptr_t& entry,
                            const item_algorithm::ItemOperationContext& context) override;
  void on_item_count_changed(int32_t type_id, const item_algorithm::item_entry_ptr_t& entry, int64_t guid,
                             const item_algorithm::ItemGridPosition& position, int64_t old_count, int64_t new_count,
                             int64_t type_total_count, const item_algorithm::ItemOperationContext& context) override;

 private:
  user* owner_ = nullptr;
  std::string log_category_prefix_;
  bool registered_ = false;
};

/// @brief 有限格子容器 (背包: 行列有限, 道具按配置占 1..N 格)
class user_item_finite_container
    : public user_item_container<item_algorithm::ItemFiniteGridContainer, user_item_finite_container> {
 public:
  using base_type = user_item_container<item_algorithm::ItemFiniteGridContainer, user_item_finite_container>;

  explicit user_item_finite_container(user* owner, const std::string& log_category_prefix = "Item.UserInventory");
  ~user_item_finite_container() override;

  void init(int32_t row_size, int32_t column_size,
            PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid);
};

/// @brief 无限格子容器 (装备槽这类: 不限行列, 每件占一个位置)
class user_item_infinite_container
    : public user_item_container<item_algorithm::ItemInfiniteGridContainer, user_item_infinite_container> {
 public:
  using base_type = user_item_container<item_algorithm::ItemInfiniteGridContainer, user_item_infinite_container>;

  explicit user_item_infinite_container(user* owner, const std::string& log_category_prefix = "Item.UserEquipment");
  ~user_item_infinite_container() override;

  void init(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid);
};

/// @brief 无位置容器 (只按道具类型记录数量)
class user_item_no_position_container
    : public user_item_container<item_algorithm::ItemNoPositionContainer, user_item_no_position_container> {
 public:
  using base_type = user_item_container<item_algorithm::ItemNoPositionContainer, user_item_no_position_container>;

  explicit user_item_no_position_container(user* owner, const std::string& log_category_prefix = "Item.UserNoPosition");
  ~user_item_no_position_container() override;

  /// @brief 初始化无位置容器
  void init(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid);
};
