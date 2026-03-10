// Copyright 2025 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_type_trait_setting.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace excel {
struct config_group_t;
}

ITEM_ALGORITHM_NAMESPACE_BEGIN

// ============================================================
// 操作原因枚举
// ============================================================

/// @brief 操作原因枚举, 用于钩子函数中区分当前操作的上下文
enum class ItemGridOperationReason : int32_t {
  kAdd = 0,          ///< 普通 add 操作
  kSub = 1,          ///< 普通 sub 操作
  kLoad = 2,         ///< 从持久化数据加载
  kMoveSub = 3,      ///< Move 操作的 Sub 阶段
  kMoveAdd = 4,      ///< Move 操作的 Add 阶段
  kApplyRemove = 5,  ///< apply_entries 的删除阶段
  kApplyUpdate = 6,  ///< apply_entries 的更新阶段
};

// ============================================================
// 基础数据类型
// ============================================================

/// @brief 算法运行时使用的简单二维坐标, 不依赖 protobuf
struct ITEM_ALGORITHM_API ItemGridPosition {
  int32_t x = 0;
  int32_t y = 0;

  inline bool operator==(const ItemGridPosition& other) const noexcept { return x == other.x && y == other.y; }
  inline bool operator!=(const ItemGridPosition& other) const noexcept { return !(*this == other); }
};

/// @brief ItemGridPosition 的哈希仿函数
struct ITEM_ALGORITHM_API ItemGridPositionHash {
  inline size_t operator()(const ItemGridPosition& pos) const noexcept {
    // 使用简单的位移组合, 对两个 int32_t 足够
    size_t h = std::hash<int32_t>{}(pos.x);
    h ^= std::hash<int32_t>{}(pos.y) + size_t{0x9e3779b9} + (h << 6) + (h >> 2);
    return h;
  }
};

/// @brief ItemGridPosition 的相等比较仿函数
struct ITEM_ALGORITHM_API ItemGridPositionEqualTo {
  inline bool operator()(const ItemGridPosition& lhs, const ItemGridPosition& rhs) const noexcept { return lhs == rhs; }
};

struct ITEM_ALGORITHM_API ItemGridEntry {
  uint64_t entry_id = 0;  ///< 每个 Grid 独立的自增 ID, 在 make_entry 时由 ItemGridAlgorithm 赋值 (1 起始)
  PROJECT_NAMESPACE_ID::DItemInstance item_instance;

  ItemGridEntry() = default;
  explicit ItemGridEntry(PROJECT_NAMESPACE_ID::DItemInstance&& inst) : item_instance(std::move(inst)) {}
};

using item_grid_entry_ptr_t = ::excel::excel_config_type_traits::shared_ptr<ItemGridEntry>;

struct ITEM_ALGORITHM_API ItemGridAddRequest {
  const PROJECT_NAMESPACE_ID::DItemInstance* item_instance = nullptr;
};

struct ITEM_ALGORITHM_API ItemGridSubRequest {
  const PROJECT_NAMESPACE_ID::DItemBasic* item_basic = nullptr;;
};

struct ITEM_ALGORITHM_API ItemGridMoveSubRequest {
  item_grid_entry_ptr_t entry;
  int32_t op_count;

  // Helper Field
  ItemGridPosition position;
  int32_t item_row;
  int32_t item_col;
};

struct ITEM_ALGORITHM_API ItemGridMoveAddRequest {
  item_grid_entry_ptr_t entry;
  PROJECT_NAMESPACE_ID::DItemPosition goal_position;
  int32_t op_count;

  // Helper Field
  int32_t type_id;
  ItemGridPosition position;
  int32_t item_row;
  int32_t item_col;
  int64_t accumulation_limit;
};

struct ITEM_ALGORITHM_API ItemGridMoveRequest {
  // Entrys 内 Entry 不可重复
  std::vector<ItemGridMoveSubRequest> move_sub_entrys;
  std::vector<ItemGridMoveAddRequest> move_add_entrys;
};

struct ITEM_ALGORITHM_API ItemGridOperationResult {
  int32_t error_code = PROJECT_NAMESPACE_ID::EN_SUCCESS;
  // 操作失败时, 表示第几个请求失败(从0开始), -1表示整体失败
  int32_t failed_index = -1;
};

/// @brief check_add 的返回包装, 直接作为 add 的入参
struct ITEM_ALGORITHM_API ItemGridAddCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  const std::vector<ItemGridAddRequest>& requests;
  ItemGridOperationResult result;

  ItemGridAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      const std::vector<ItemGridAddRequest>& in_requests)
      : config_group(in_config_group), requests(in_requests) {}
};

/// @brief check_sub 的返回包装, 直接作为 sub 的入参
struct ITEM_ALGORITHM_API ItemGridSubCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  const std::vector<ItemGridSubRequest>& requests;
  ItemGridOperationResult result;

  ItemGridSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      const std::vector<ItemGridSubRequest>& in_requests)
      : config_group(in_config_group), requests(in_requests) {}
};

/// @brief check_move 的返回包装, 直接作为 move 的入参
struct ITEM_ALGORITHM_API ItemGridMoveCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  const ItemGridMoveRequest& request;
  int32_t error_code;

  ItemGridMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      const ItemGridMoveRequest& in_request)
      : config_group(in_config_group), request(in_request), error_code(PROJECT_NAMESPACE_ID::EN_SUCCESS) {}
};

ITEM_ALGORITHM_NAMESPACE_END
