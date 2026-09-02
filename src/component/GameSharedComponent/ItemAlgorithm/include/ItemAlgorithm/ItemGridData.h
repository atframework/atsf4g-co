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
#include <memory/rc_ptr.h>

#include <cstdint>
#include <functional>
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

// ============================================================
// 操作原因枚举
// ============================================================

/// @brief 操作原因枚举, 用于钩子函数中区分当前操作的上下文
enum class ItemGridOperationReason : int32_t {
  kAdd = 0,                 ///< 普通 add 操作
  kSub = 1,                 ///< 普通 sub 操作
  kLoad = 2,                ///< 从持久化数据加载
  kMoveSub = 3,             ///< Move 操作的 Sub 阶段
  kMoveAdd = 4,             ///< Move 操作的 Add 阶段
  kApplyRemove = 5,         ///< apply_entries 的删除阶段
  kApplyUpdate = 6,         ///< apply_entries 的更新阶段
  kModifyInstanceData = 7,  ///< 修改 Entry 的数据, 例如修改 Guid/Count 等
  kReplaceSub = 8,          ///< Replace 操作的 Sub 阶段 (整体移除现有条目)
  kReplaceAdd = 9,          ///< Replace 操作的 Add 阶段 (整体放入新列表)
};

// ============================================================
// 基础数据类型
// ============================================================

struct ITEM_ALGORITHM_API ItemGridPosition {
  int32_t x = 0;
  int32_t y = 0;

  inline bool operator==(const ItemGridPosition& other) const noexcept { return x == other.x && y == other.y; }
  inline bool operator!=(const ItemGridPosition& other) const noexcept { return !(*this == other); }
};

struct ITEM_ALGORITHM_API ItemGridPositionHash {
  inline size_t operator()(const ItemGridPosition& pos) const noexcept {
    // 使用简单的位移组合, 对两个 int32_t 足够
    size_t h = std::hash<int32_t>{}(pos.x);
    h ^= std::hash<int32_t>{}(pos.y) + size_t{0x9e3779b9} + (h << 6) + (h >> 2);
    return h;
  }
};

struct ITEM_ALGORITHM_API ItemGridPositionEqualTo {
  inline bool operator()(const ItemGridPosition& lhs, const ItemGridPosition& rhs) const noexcept { return lhs == rhs; }
};

class ItemGridAlgorithm;

struct ITEM_ALGORITHM_API ItemGridEntry : public atfw::util::memory::enable_shared_rc_from_this<ItemGridEntry> {
  explicit ItemGridEntry(atfw::util::memory::strong_rc_ptr<ItemGridAlgorithm> in_belong_grid,
                         PROJECT_NAMESPACE_ID::DItemInstance&& inst, uint64_t in_entry_id);
  ~ItemGridEntry();
  uint64_t entry_id() const;
  const PROJECT_NAMESPACE_ID::DItemInstance& item_instance() const;
  PROJECT_NAMESPACE_ID::DItemData& mutable_item_data();
  uint64_t get_sort_key() const;

 private:
  friend class ItemGridAlgorithm;
  PROJECT_NAMESPACE_ID::DItemBasic& mutable_item_basic();
  PROJECT_NAMESPACE_ID::DItemInstance& mutable_item_instance();

 private:
  uint64_t entry_id_ = 0;
  PROJECT_NAMESPACE_ID::DItemInstance item_instance_;
  atfw::util::memory::weak_rc_ptr<ItemGridAlgorithm> belong_grid_;
};

using item_grid_entry_ptr_t = atfw::util::memory::strong_rc_ptr<ItemGridEntry>;
using item_grid_entry_weak_ptr_t = atfw::util::memory::weak_rc_ptr<ItemGridEntry>;

using ItemGridAddRequest = google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>;
using ItemGridSubRequest = google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>;
using ItemGridReplaceRequest = google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>;
using ItemGridHasRequest = google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>;

struct item_grid_entry_ptr_comparator {
  bool operator()(const item_grid_entry_ptr_t& l, const item_grid_entry_ptr_t& r) const {
    return l->get_sort_key() < r->get_sort_key();
  }
};

struct ITEM_ALGORITHM_API ItemGridMoveSubRequest {
  item_grid_entry_ptr_t entry;
  int32_t op_count = 0;

  // Helper Field
  ItemGridPosition position = {};
  int32_t item_row = 0;
  int32_t item_col = 0;
};

struct ITEM_ALGORITHM_API ItemGridMoveAddRequest {
  item_grid_entry_ptr_t entry;  // Entry ID 无效 仅使用instance
  PROJECT_NAMESPACE_ID::DItemPosition goal_position;
  int32_t op_count = 0;

  // Helper Field
  int32_t type_id = 0;
  ItemGridPosition position = {};
  int32_t item_row = 0;
  int32_t item_col = 0;
  int64_t accumulation_limit = 0;
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

struct ITEM_ALGORITHM_API ItemGridAddCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridAddRequest requests;
  ItemGridOperationResult result;
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ItemGridAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridAddRequest&& in_requests, int64_t in_container_guid, int64_t in_operate_id)
      : config_group(in_config_group),
        requests(std::move(in_requests)),
        container_guid(in_container_guid),
        operate_id(in_operate_id) {}
  ItemGridAddCheckedRequest(const ItemGridAddCheckedRequest&) = delete;
  ItemGridAddCheckedRequest& operator=(const ItemGridAddCheckedRequest&) = delete;
  ItemGridAddCheckedRequest(ItemGridAddCheckedRequest&&) = default;
  ItemGridAddCheckedRequest& operator=(ItemGridAddCheckedRequest&&) = default;
};

struct ITEM_ALGORITHM_API ItemGridSubCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridSubRequest requests;
  ItemGridOperationResult result;
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ItemGridSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridSubRequest&& in_requests, int64_t in_container_guid, int64_t in_operate_id)
      : config_group(in_config_group),
        requests(std::move(in_requests)),
        container_guid(in_container_guid),
        operate_id(in_operate_id) {}
  ItemGridSubCheckedRequest(const ItemGridSubCheckedRequest&) = delete;
  ItemGridSubCheckedRequest& operator=(const ItemGridSubCheckedRequest&) = delete;
  ItemGridSubCheckedRequest(ItemGridSubCheckedRequest&&) = default;
  ItemGridSubCheckedRequest& operator=(ItemGridSubCheckedRequest&&) = default;
};

struct ITEM_ALGORITHM_API ItemGridMoveCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridMoveRequest request;
  ItemGridOperationResult result;
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ItemGridMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridMoveRequest&& in_request, int64_t in_container_guid, int64_t in_operate_id)
      : config_group(in_config_group),
        request(std::move(in_request)),
        container_guid(in_container_guid),
        operate_id(in_operate_id) {}
  ItemGridMoveCheckedRequest(const ItemGridMoveCheckedRequest&) = delete;
  ItemGridMoveCheckedRequest& operator=(const ItemGridMoveCheckedRequest&) = delete;
  ItemGridMoveCheckedRequest(ItemGridMoveCheckedRequest&&) = default;
  ItemGridMoveCheckedRequest& operator=(ItemGridMoveCheckedRequest&&) = default;
};

struct ITEM_ALGORITHM_API ItemGridReplaceCheckedRequest {
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridReplaceRequest requests;
  ItemGridOperationResult result;
  bool apply = false;
  int64_t container_guid = 0;
  int64_t operate_id = 0;

  ItemGridReplaceCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridReplaceRequest&& in_requests, int64_t in_container_guid, int64_t in_operate_id)
      : config_group(in_config_group),
        requests(std::move(in_requests)),
        container_guid(in_container_guid),
        operate_id(in_operate_id) {}
  ItemGridReplaceCheckedRequest(const ItemGridReplaceCheckedRequest&) = delete;
  ItemGridReplaceCheckedRequest& operator=(const ItemGridReplaceCheckedRequest&) = delete;
  ItemGridReplaceCheckedRequest(ItemGridReplaceCheckedRequest&&) = default;
  ItemGridReplaceCheckedRequest& operator=(ItemGridReplaceCheckedRequest&&) = default;
};

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
