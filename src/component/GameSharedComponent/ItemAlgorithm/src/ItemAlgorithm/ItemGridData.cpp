// Copyright 2026 atframework

#include "ItemAlgorithm/ItemGridData.h"
#include "ItemAlgorithm/ItemGridAlgorithm.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

ITEM_ALGORITHM_API ItemGridEntry::ItemGridEntry(atfw::util::memory::strong_rc_ptr<ItemGridAlgorithm> in_belong_grid,
                                                PROJECT_NAMESPACE_ID::DItemInstance&& inst, uint64_t in_entry_id)
    : entry_id_(in_entry_id), item_instance_(std::move(inst)), belong_grid_(in_belong_grid) {}

ITEM_ALGORITHM_API ItemGridEntry::~ItemGridEntry() {
  if (entry_id_ != 0) {
    if (auto grid = belong_grid_.lock()) {
      grid->remove_entry_id_index(entry_id_);
    }
  }
}

ITEM_ALGORITHM_API uint64_t ItemGridEntry::entry_id() const { return entry_id_; }

ITEM_ALGORITHM_API uint64_t ItemGridEntry::get_sort_key() const {
  switch (item_instance_.item_basic().position().grid_position().position_type_case()) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory:
      return (static_cast<uint64_t>(item_instance_.item_basic().position().grid_position().user_inventory().y())
              << 32) +
             (static_cast<uint64_t>(item_instance_.item_basic().position().grid_position().user_inventory().x()) &
              0xFFFFFFFF);
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory:
      return (static_cast<uint64_t>(item_instance_.item_basic().position().grid_position().character_inventory().y())
              << 32) +
             (static_cast<uint64_t>(item_instance_.item_basic().position().grid_position().character_inventory().x()) &
              0xFFFFFFFF);
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment:
      return static_cast<uint64_t>(
          item_instance_.item_basic().position().grid_position().character_equipment().slot_idx());
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory:
      return 0;
    default:
      break;
  }
  return 0;
}

ITEM_ALGORITHM_API const PROJECT_NAMESPACE_ID::DItemInstance& ItemGridEntry::item_instance() const {
  return item_instance_;
}

ITEM_ALGORITHM_API PROJECT_NAMESPACE_ID::DItemData& ItemGridEntry::mutable_item_data() {
  if (auto grid = belong_grid_.lock()) {
    grid->on_item_data_changed(shared_from_this(), ItemGridOperationReason::kModifyInstanceData);
  }
  return *item_instance_.mutable_item_data();
}

PROJECT_NAMESPACE_ID::DItemBasic& ItemGridEntry::mutable_item_basic() { return *item_instance_.mutable_item_basic(); }

PROJECT_NAMESPACE_ID::DItemInstance& ItemGridEntry::mutable_item_instance() { return item_instance_; }

// ============================================================
// Checked Request 特殊成员实现 (导出符号)
// ============================================================

ITEM_ALGORITHM_API ItemGridAddCheckedRequest::ItemGridAddCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    ItemGridAddRequest&& in_requests, int64_t in_container_guid, int64_t in_operate_id)
    : config_group(in_config_group),
      requests(std::move(in_requests)),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemGridAddCheckedRequest::ItemGridAddCheckedRequest(ItemGridAddCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(std::move(other.requests)),
      result(other.result),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemGridAddCheckedRequest& ItemGridAddCheckedRequest::operator=(ItemGridAddCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    requests = std::move(other.requests);
    result = other.result;
    apply = other.apply;
    container_guid = other.container_guid;
    operate_id = other.operate_id;
  }
  return *this;
}

ITEM_ALGORITHM_API ItemGridSubCheckedRequest::ItemGridSubCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    ItemGridSubRequest&& in_requests, int64_t in_container_guid, int64_t in_operate_id)
    : config_group(in_config_group),
      requests(std::move(in_requests)),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemGridSubCheckedRequest::ItemGridSubCheckedRequest(ItemGridSubCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(std::move(other.requests)),
      result(other.result),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemGridSubCheckedRequest& ItemGridSubCheckedRequest::operator=(ItemGridSubCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    requests = std::move(other.requests);
    result = other.result;
    apply = other.apply;
    container_guid = other.container_guid;
    operate_id = other.operate_id;
  }
  return *this;
}

ITEM_ALGORITHM_API ItemGridMoveCheckedRequest::ItemGridMoveCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    ItemGridMoveRequest&& in_request, int64_t in_container_guid, int64_t in_operate_id)
    : config_group(in_config_group),
      request(std::move(in_request)),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemGridMoveCheckedRequest::ItemGridMoveCheckedRequest(ItemGridMoveCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      request(std::move(other.request)),
      result(other.result),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemGridMoveCheckedRequest& ItemGridMoveCheckedRequest::operator=(ItemGridMoveCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    request = std::move(other.request);
    result = other.result;
    apply = other.apply;
    container_guid = other.container_guid;
    operate_id = other.operate_id;
  }
  return *this;
}

ITEM_ALGORITHM_API ItemGridReplaceCheckedRequest::ItemGridReplaceCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    ItemGridReplaceRequest&& in_requests, int64_t in_container_guid, int64_t in_operate_id)
    : config_group(in_config_group),
      requests(std::move(in_requests)),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemGridReplaceCheckedRequest::ItemGridReplaceCheckedRequest(ItemGridReplaceCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(std::move(other.requests)),
      result(other.result),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemGridReplaceCheckedRequest& ItemGridReplaceCheckedRequest::operator=(
    ItemGridReplaceCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    requests = std::move(other.requests);
    result = other.result;
    apply = other.apply;
    container_guid = other.container_guid;
    operate_id = other.operate_id;
  }
  return *this;
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
