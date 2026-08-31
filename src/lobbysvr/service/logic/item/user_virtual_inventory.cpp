// Copyright 2026 atframework

#include "logic/item/user_virtual_inventory.h"

#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <config/excel/config_manager.h>
#include <data/user.h>
#include <logic/item/user_item_grid_manager.h>

#include <algorithm>

user_virtual_inventory_grid::user_virtual_inventory_grid(user* owner)
    : user_item_grid_algorithm(owner, "Item.UserVirtualInventory") {}
user_virtual_inventory_grid::~user_virtual_inventory_grid() = default;

item_algorithm::item_grid_algorithm_ptr_t user_virtual_inventory_grid::create_empty_clone() const {
  auto clone = atfw::component::memory::stl::make_strong_rc<user_virtual_inventory_grid>(get_owner());
  copy_empty_config_to(*clone);
  return clone;
}

user_virtual_inventory::user_virtual_inventory(user* owner)
    : owner_(owner), virtual_grid_(atfw::component::memory::stl::make_strong_rc<user_virtual_inventory_grid>(owner)) {}

void user_virtual_inventory::init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data) {
  for (const auto& item : data.items()) {
    virtual_grid_->load(excel::get_current_config_group(), item);
  }
}

void user_virtual_inventory::dump(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const {
  virtual_grid_->foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    *out.add_items() = inst;
    return true;
  });
}
