// Copyright 2026 atframework

#include "logic/item/user_virtual_inventory.h"

#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <config/excel/config_manager.h>
#include <data/user.h>
#include <logic/item/user_item_grid_manager.h>

#include <algorithm>
#include <utility>

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

void user_virtual_inventory::init_grid() {
  if (grid_inited_) {
    return;
  }
  grid_inited_ = true;

  // 虚拟道具仓库只有一个无位置网格, 容器GUID不持久化, 创建或登录时分配
  virtual_grid_->init(0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory,
                      owner_->get_user_item_grid_manager().allocate_container_guid());
}

void user_virtual_inventory::init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data) {
  init_grid();

  // 本接口可重复调用, 载入前先清理旧数据
  virtual_grid_->clear();
  for (const auto& item : data.items()) {
    virtual_grid_->load(excel::get_current_config_group(), item);
  }
}

void user_virtual_inventory::dump(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const {
  out.Clear();
  dump(*out.mutable_items());
}

void user_virtual_inventory::dump(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out) const {
  out.Clear();
  virtual_grid_->foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    *out.Add() = inst;
    return true;
  });
}

void user_virtual_inventory::replace(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& items) {
  item_algorithm::ItemGridReplaceRequest requests;
  for (const auto& item : items) {
    *requests.Add() = item;
  }

  auto checked_request = virtual_grid_->check_replace(excel::get_current_config_group(), std::move(requests));
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWLOGERROR("user_virtual_inventory replace check_replace failed, error: {}({})", checked_request.result.error_code,
               PROJECT_NAMESPACE_ID::EnErrorCode_Name(checked_request.result.error_code));
    return;
  }

  auto result = virtual_grid_->replace(checked_request);
  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWLOGERROR("user_virtual_inventory replace failed, error: {}({})", result.error_code,
               PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
  }
}
