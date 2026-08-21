// Copyright 2026 atframework

#include "ItemAlgorithm/ItemGridData.h"
#include "ItemAlgorithm/ItemGridAlgorithm.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

ItemGridEntry::ItemGridEntry(atfw::util::memory::strong_rc_ptr<ItemGridAlgorithm> in_belong_grid,
                             PROJECT_NAMESPACE_ID::DItemInstance&& inst, uint64_t in_entry_id)
    : entry_id_(in_entry_id), item_instance_(std::move(inst)), belong_grid_(in_belong_grid) {}

ItemGridEntry::~ItemGridEntry() {
  if (entry_id_ != 0) {
    if (auto grid = belong_grid_.lock()) {
      grid->remove_entry_id_index(entry_id_);
    }
  }
}

uint64_t ItemGridEntry::entry_id() const { return entry_id_; }

const PROJECT_NAMESPACE_ID::DItemInstance& ItemGridEntry::item_instance() const { return item_instance_; }

PROJECT_NAMESPACE_ID::DItemData& ItemGridEntry::mutable_item_data() {
  if (auto grid = belong_grid_.lock()) {
    grid->on_item_data_changed(shared_from_this(), ItemGridOperationReason::kModifyInstanceData);
  }
  return *item_instance_.mutable_item_data();
}

PROJECT_NAMESPACE_ID::DItemBasic& ItemGridEntry::mutable_item_basic() { return *item_instance_.mutable_item_basic(); }

PROJECT_NAMESPACE_ID::DItemInstance& ItemGridEntry::mutable_item_instance() { return item_instance_; }

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END