// Copyright 2025 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_type_trait_setting.h>
#include <memory/rc_ptr.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"
#include "ItemAlgorithm/ItemContainerEntry.h"
#include "ItemAlgorithm/ItemIterable.h"

namespace excel {
struct config_group_t;
}

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

class ItemContainer;

/// @brief 配置组句柄 (各操作接口的第一个参数)
using excel_config_group_ptr_t = ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>;

// ============================================================
// 操作原因枚举
// ============================================================

/// @brief 操作原因枚举, 用于钩子函数中区分当前操作的上下文
enum class ItemOperationReason : int32_t {
  kAdd = 0,                 ///< 普通 add 操作
  kSub = 1,                 ///< 普通 sub 操作
  kLoad = 2,                ///< 从持久化数据加载
  kMoveSub = 3,             ///< Move 操作的 Sub 阶段
  kMoveAdd = 4,             ///< Move 操作的 Add 阶段
  kApplyRemove = 5,         ///< apply_entries 的删除阶段
  kApplyUpdate = 6,         ///< apply_entries 的更新阶段
  kModifyInstanceData = 7,  ///< 修改 Entry 的数据, 例如修改 Guid/Count 等
  kReplaceLoad = 8,         ///< 整体替换时 clear 之后逐条 load (由 load 传入)
  kClear = 9,               ///< clear() 整体清空 (逐条移除并通知, 与 sub 的整体移除一致)
};

// ============================================================
// 操作上下文
// ============================================================

/// @brief 操作来源 — 由操作接口的调用方传入
struct ATFW_UTIL_SYMBOL_VISIBLE ItemOperationSource {
  int64_t major_type = 0;
  int64_t minor_type = 0;
  int64_t micro_type = 0;
};

/// @brief 钩子收到的操作上下文
///
/// 第一个字段是容器判定的操作原因 (add / sub / load / move / apply ...),
/// 其余三个字段是调用方通过操作接口传入的 ItemOperationSource。
struct ATFW_UTIL_SYMBOL_VISIBLE ItemOperationContext {
  ItemOperationReason reason = ItemOperationReason::kAdd;
  ItemOperationSource source;
};

// ============================================================
// 基础数据类型
// ============================================================

/// @brief 容器内的格子坐标 (x 为列, y 为行; 单格模式只用 x 作为槽位)
struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridPosition {
  int32_t x = 0;
  int32_t y = 0;

  ATFW_UTIL_FORCEINLINE bool operator==(const ItemGridPosition& other) const noexcept {
    return x == other.x && y == other.y;
  }
  ATFW_UTIL_FORCEINLINE bool operator!=(const ItemGridPosition& other) const noexcept { return !(*this == other); }
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridPositionHash {
  ATFW_UTIL_FORCEINLINE size_t operator()(const ItemGridPosition& pos) const noexcept {
    // 使用简单的位移组合, 对两个 int32_t 足够
    size_t h = std::hash<int32_t>{}(pos.x);
    h ^= std::hash<int32_t>{}(pos.y) + size_t{0x9e3779b9} + (h << 6) + (h >> 2);
    return h;
  }
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridPositionEqualTo {
  ATFW_UTIL_FORCEINLINE bool operator()(const ItemGridPosition& lhs, const ItemGridPosition& rhs) const noexcept {
    return lhs == rhs;
  }
};

/// @brief 容器操作接口的请求输入类型
///
/// 操作接口接的是迭代器视图, 调用方可以传 RepeatedPtrField / std::vector / 数组 / gsl::span
/// 等任意可遍历容器。checked request 只**引用**调用方传入的视图 (不复制数据), 且接口参数是
/// 非 const 引用 —— 临时视图 (如 make_item_readable_iterable(x)) 无法绑定, 所以调用方必须自己
/// 持有一个具名视图, 并保证它活到 check 通过后的执行阶段。
using item_instance_readable_iterable = item_readable_iterable<PROJECT_NAMESPACE_ID::DItemInstance>;
using item_basic_readable_iterable = item_readable_iterable<PROJECT_NAMESPACE_ID::DItemBasic>;
using item_instance_entry_readable_iterable = item_readable_iterable<PROJECT_NAMESPACE_ID::DItemInstanceEntry>;

struct ATFW_UTIL_SYMBOL_VISIBLE item_entry_ptr_comparator {
  ATFW_UTIL_FORCEINLINE bool operator()(const item_entry_ptr_t& l, const item_entry_ptr_t& r) const {
    // 排序键由所属容器写入 (容器按自己的位置语义计算), 条目本身不理解它
    uint64_t lhs_key = l->sort_key();
    uint64_t rhs_key = r->sort_key();
    if (lhs_key != rhs_key) {
      return lhs_key < rhs_key;
    }
    // 键相同的条目 (不占格道具, 或位置字段未落在容器已支持的字段上) 用 entry_id 兜底。
    // 否则 std::set 会把它们当成同一个键, 后插入的条目被静默丢弃。
    return l->entry_id() < r->entry_id();
  }
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemMoveSubRequest {
  item_entry_ptr_t entry;
  int32_t op_count = 0;

  // Helper Field
  ItemGridPosition position = {};
  int32_t item_row = 0;
  int32_t item_col = 0;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemMoveAddRequest {
  item_entry_ptr_t entry;  // Entry ID 无效 仅使用instance
  PROJECT_NAMESPACE_ID::DItemPosition goal_position;
  int32_t op_count = 0;

  // Helper Field
  int32_t type_id = 0;
  ItemGridPosition position = {};
  int32_t item_row = 0;
  int32_t item_col = 0;
  int64_t accumulation_limit = 0;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemMoveRequest {
  // Entrys 内 Entry 不可重复
  std::vector<ItemMoveSubRequest> move_sub_entrys;
  std::vector<ItemMoveAddRequest> move_add_entrys;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemOperationResult {
  int32_t error_code = PROJECT_NAMESPACE_ID::EN_SUCCESS;
  // 失败时第一个"数量不足"的道具类型 ID; 0 表示本次失败与数量不足无关
  int32_t failed_type_id = 0;
};

// ============================================================
// 容器级 Checked Request (check 通过后交给 add/sub/move 执行)
// ============================================================

struct ATFW_UTIL_SYMBOL_VISIBLE ItemAddCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  /// @brief 调用方传入的请求视图 (只引用, 不复制数据; 调用方需保证它活到 add 执行完)
  item_instance_readable_iterable& requests;
  ItemOperationResult result;
  ItemOperationSource source;  ///< 调用方通过 check_add 传入, add 时透传给钩子
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ITEM_ALGORITHM_API ItemAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      item_instance_readable_iterable& in_requests, int64_t in_container_guid, int64_t in_operate_id,
      const ItemOperationSource& in_source = ItemOperationSource{});
  ItemAddCheckedRequest(const ItemAddCheckedRequest&) = delete;
  ItemAddCheckedRequest& operator=(const ItemAddCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemAddCheckedRequest(ItemAddCheckedRequest&&) noexcept;
  ItemAddCheckedRequest& operator=(ItemAddCheckedRequest&&) = delete;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemSubCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  /// @brief 调用方传入的请求视图 (只引用, 不复制数据; 调用方需保证它活到 sub 执行完)
  item_basic_readable_iterable& requests;
  ItemOperationResult result;
  ItemOperationSource source;  ///< 调用方通过 check_sub 传入, sub 时透传给钩子
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ITEM_ALGORITHM_API ItemSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      item_basic_readable_iterable& in_requests, int64_t in_container_guid, int64_t in_operate_id,
      const ItemOperationSource& in_source = ItemOperationSource{});
  ItemSubCheckedRequest(const ItemSubCheckedRequest&) = delete;
  ItemSubCheckedRequest& operator=(const ItemSubCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemSubCheckedRequest(ItemSubCheckedRequest&&) noexcept;
  ItemSubCheckedRequest& operator=(ItemSubCheckedRequest&&) = delete;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemMoveCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemMoveRequest request;
  ItemOperationResult result;
  ItemOperationSource source;  ///< 调用方通过 check_move 传入, move 时透传给钩子
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ITEM_ALGORITHM_API ItemMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemMoveRequest&& in_request, int64_t in_container_guid, int64_t in_operate_id,
      const ItemOperationSource& in_source = ItemOperationSource{});
  ItemMoveCheckedRequest(const ItemMoveCheckedRequest&) = delete;
  ItemMoveCheckedRequest& operator=(const ItemMoveCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemMoveCheckedRequest(ItemMoveCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemMoveCheckedRequest& operator=(ItemMoveCheckedRequest&&) = delete;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
