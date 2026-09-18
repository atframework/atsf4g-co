// Copyright 2026 atframework

#include <ItemAlgorithm/ItemContainer.h>
#include <ItemAlgorithm/ItemContainerEntry.h>

#include <utility>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

ITEM_ALGORITHM_API ItemEntry::ItemEntry(atfw::util::memory::strong_rc_ptr<ItemContainer> in_belong_container,
                                        PROJECT_NAMESPACE_ID::DItemInstance&& inst, uint64_t in_entry_id)
    : entry_id_(in_entry_id), item_instance_(std::move(inst)), belong_container_(in_belong_container) {}

ITEM_ALGORITHM_API ItemEntry::~ItemEntry() {
  if (entry_id_ != 0) {
    if (auto container = belong_container_.lock()) {
      container->remove_entry_id_index(entry_id_);
    }
  }
}

ITEM_ALGORITHM_API uint64_t ItemEntry::entry_id() const { return entry_id_; }

ITEM_ALGORITHM_API uint64_t ItemEntry::sort_key() const { return sort_key_; }

ITEM_ALGORITHM_API void ItemEntry::set_sort_key(uint64_t key) { sort_key_ = key; }

ITEM_ALGORITHM_API const PROJECT_NAMESPACE_ID::DItemInstance& ItemEntry::item_instance() const {
  return item_instance_;
}

ITEM_ALGORITHM_API PROJECT_NAMESPACE_ID::DItemData& ItemEntry::mutable_item_data() {
  return *item_instance_.mutable_item_data();
}

ITEM_ALGORITHM_API void ItemEntry::mark_item_data_dirty() {
  if (auto container = belong_container_.lock()) {
    const ItemOperationContext context{ItemOperationReason::kModifyInstanceData, ItemOperationSource{}};
    container->on_item_data_changed(shared_from_this(), context);
  }
}

PROJECT_NAMESPACE_ID::DItemBasic& ItemEntry::mutable_item_basic() { return *item_instance_.mutable_item_basic(); }

PROJECT_NAMESPACE_ID::DItemInstance& ItemEntry::mutable_item_instance() { return item_instance_; }

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
