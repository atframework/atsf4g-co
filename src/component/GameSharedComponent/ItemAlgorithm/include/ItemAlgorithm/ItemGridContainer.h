// Copyright 2025 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>
#include <ItemAlgorithm/ItemGridAlgorithm.h>
#include <ItemAlgorithm/ItemGridData.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace excel {
struct config_group_t;
}

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief Container 层 Add 的 checked 请求
///
/// 由 check_add() 构建, 持有每个 Grid 的分组请求 (已通过 Grid 层 check)。
/// 传入 add() 以执行实际添加操作。
struct ITEM_ALGORITHM_API ItemGridContainerAddCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  const std::vector<ItemGridAddRequest>& requests;
  ItemGridOperationResult result;

  /// @brief 每个 Grid 的添加请求数据
  struct PerGridData {
    ItemGridAlgorithm* grid = nullptr;
    std::vector<ItemGridAddRequest> sub_requests;  ///< 该 Grid 上的分组请求 (owned)
    std::vector<size_t> original_indices;           ///< 映射回原始 requests 的索引
  };
  std::vector<PerGridData> grid_data;

  ItemGridContainerAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      const std::vector<ItemGridAddRequest>& in_requests)
      : config_group(in_config_group), requests(in_requests) {}
};

/// @brief Container 层 Sub 的 checked 请求
///
/// 由 check_sub() 构建, 持有每个 Grid 的分组请求 (已通过 Grid 层 check)。
/// 传入 sub() 以执行实际扣减操作。
struct ITEM_ALGORITHM_API ItemGridContainerSubCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  const std::vector<ItemGridSubRequest>& requests;
  ItemGridOperationResult result;

  /// @brief 每个 Grid 的扣减请求数据
  struct PerGridData {
    ItemGridAlgorithm* grid = nullptr;
    std::vector<ItemGridSubRequest> sub_requests;  ///< 该 Grid 上的分组请求 (owned)
    std::vector<size_t> original_indices;           ///< 映射回原始 requests 的索引
  };
  std::vector<PerGridData> grid_data;

  ItemGridContainerSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      const std::vector<ItemGridSubRequest>& in_requests)
      : config_group(in_config_group), requests(in_requests) {}
};

struct ITEM_ALGORITHM_API ItemGridContainerMoveRequest {
  PROJECT_NAMESPACE_ID::DItemBasic source_item_basic;
  PROJECT_NAMESPACE_ID::DItemPosition target_position;
};

/// @brief Move 的容器层 checked 请求
///
/// 由 check_move() 构建, 持有每个 Grid 的 ItemGridMoveRequest (已通过 Grid 层 check)。
/// 传入 move() 以执行实际移动操作。
struct ITEM_ALGORITHM_API ItemGridContainerMoveCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  const std::vector<ItemGridContainerMoveRequest>& requests;
  int32_t error_code = PROJECT_NAMESPACE_ID::EN_SUCCESS;
  int32_t failed_index = -1;

  /// @brief 每个 Grid 的移动请求数据
  struct PerGridData {
    ItemGridAlgorithm* grid = nullptr;
    ItemGridMoveRequest move_request;               ///< 该 Grid 上的聚合移动请求 (owned)
    std::vector<size_t> original_indices;            ///< 映射回原始 requests 的索引
  };
  std::vector<PerGridData> grid_data;
  std::vector<item_grid_entry_ptr_t> constructed_entries;  ///< 跨Grid移入时构造的临时 entry (生命周期管理)

  ItemGridContainerMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      const std::vector<ItemGridContainerMoveRequest>& in_requests)
      : config_group(in_config_group), requests(in_requests) {}
};

/// @brief 容器层: 管理多个 ItemGridAlgorithm, 支持跨 Grid 操作
///
/// 子类需实现 select_grid() 虚函数, 根据 DItemPosition 路由到具体 Grid。
/// 提供对 Add / Sub / Move 的批量操作:
///   - Add / Sub: 按 DItemPosition 分组后委托给各自 Grid
///   - Move: 将 ContainerMoveRequest 转换为 Grid 层的 ItemGridMoveRequest (Sub+Add 格式)
///
/// Move 转换规则:
///   - 同Grid内 Move: sub_entry=源条目, add_entry=源条目, goal_position=目标位置
///   - 跨Grid移出 (源Grid): sub_entry=源条目, add_entry=nullptr
///   - 跨Grid移入 (目标Grid): sub_entry=nullptr, add_entry=<从源复制>, goal_position=目标位置
///   - 每个 Grid 只调用一次 check_move(批量), 所有 check 通过后再执行
class ITEM_ALGORITHM_API ItemGridContainer {
 public:
  ItemGridContainer();
  virtual ~ItemGridContainer();

  ItemGridContainer(const ItemGridContainer&) = delete;
  ItemGridContainer& operator=(const ItemGridContainer&) = delete;
  ItemGridContainer(ItemGridContainer&&) = delete;
  ItemGridContainer& operator=(ItemGridContainer&&) = delete;

  // ============================================================
  // 路由虚函数 — 子类必须实现
  // ============================================================

  /// @brief 根据 DItemPosition 选择目标 ItemGridAlgorithm (可变版本, 用于执行操作)
  /// @param position 道具位置 (container_guid + grid_position)
  /// @return 目标 Grid 指针, 不存在时返回 nullptr
  virtual ItemGridAlgorithm* select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) = 0;

  /// @brief 根据 DItemPosition 选择目标 ItemGridAlgorithm (只读版本, 用于 check 阶段)
  virtual const ItemGridAlgorithm* select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) const = 0;

  // ============================================================
  // 批量操作接口
  // ============================================================

  /// @brief 批量添加道具 — 检查阶段 (不修改数据)
  ///
  /// 按 DItemPosition 分组路由到各 Grid, 调用 Grid::check_add() 验证。
  /// 返回的 checked 请求可直接传入 add() 执行。
  ///
  /// @param config_group 配置组
  /// @param requests     添加请求数组 (每个请求的 DItemInstance 携带 DItemPosition 用于路由)
  /// @return Checked 请求 (result.error_code == EN_SUCCESS 表示可执行)
  ItemGridContainerAddCheckedRequest check_add(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const std::vector<ItemGridAddRequest>& requests);

  /// @brief 批量添加道具 — 执行阶段
  ///
  /// 基于已通过 check_add() 验证的 checked_request 执行实际添加。
  ///
  /// @param checked_request  check_add() 返回的已验证请求
  /// @return 操作结果 (failed_index 为原始 requests 中的索引)
  ItemGridOperationResult add(const ItemGridContainerAddCheckedRequest& checked_request);

  /// @brief 批量扣减道具 — 检查阶段 (不修改数据)
  ///
  /// 按 DItemPosition 分组路由到各 Grid, 调用 Grid::check_sub() 验证。
  /// 返回的 checked 请求可直接传入 sub() 执行。
  ///
  /// @param config_group 配置组
  /// @param requests     扣减请求数组 (每个请求的 DItemBasic 携带 DItemPosition 用于路由)
  /// @return Checked 请求 (result.error_code == EN_SUCCESS 表示可执行)
  ItemGridContainerSubCheckedRequest check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const std::vector<ItemGridSubRequest>& requests);

  /// @brief 批量扣减道具 — 执行阶段
  ///
  /// 基于已通过 check_sub() 验证的 checked_request 执行实际扣减。
  ///
  /// @param checked_request  check_sub() 返回的已验证请求
  /// @return 操作结果 (failed_index 为原始 requests 中的索引)
  ItemGridOperationResult sub(const ItemGridContainerSubCheckedRequest& checked_request);

  /// @brief 批量移动道具 — 检查阶段 (不修改数据)
  ///
  /// 将 ContainerMoveRequest 转换为 Grid 层的 ItemGridMoveRequest (Sub+Add)。
  /// 同Grid内Move: sub + add 使用同一源条目。跨Grid: 源Grid sub-only, 目标Grid add-only。
  /// 容器层验证: 参数合法性、源条目存在性、移动数量不超过持有数量。
  /// Grid层验证: 位置可行性 (占用/碰撞/范围)。
  ///
  /// @param config_group 配置组
  /// @param requests     移动请求数组
  /// @return Checked 请求 (error_code == EN_SUCCESS 表示可执行, 否则 failed_index 指示失败请求)
  ItemGridContainerMoveCheckedRequest check_move(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const std::vector<ItemGridContainerMoveRequest>& requests);

  /// @brief 批量移动道具 — 执行阶段
  ///
  /// 基于已通过 check_move() 验证的 checked_request 执行实际移动。
  /// 每个 Grid 只调用一次 Grid::move()。
  ///
  /// @param checked_request  check_move() 返回的已验证请求
  /// @return 操作结果
  ItemGridOperationResult move(const ItemGridContainerMoveCheckedRequest& checked_request);
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
