// Copyright 2026 atframework

#include "logic/item/user_virtual_inventory.h"

#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <config/excel/config_manager.h>
#include <data/user.h>
#include <logic/item/user_item_container_manager.h>

#include <algorithm>
#include <utility>

user_virtual_inventory::user_virtual_inventory(user* owner)
    : owner_(owner),
      virtual_container_(atfw::component::memory::stl::make_strong_rc<user_virtual_inventory_container>(owner)) {}

void user_virtual_inventory::init_container() {
  // 虚拟道具仓库只有一个无位置网格, 容器GUID不持久化, 创建时分配
  virtual_container_->init(PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory,
                           owner_->get_user_item_container_manager().allocate_container_guid());
}

void user_virtual_inventory::init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data) {
  virtual_container_->init(PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, data.container_guid());
  for (const auto& item : data.items()) {
    virtual_container_->load(excel::get_current_config_group(), item);
  }
}

void user_virtual_inventory::dump(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const {
  out.Clear();
  dump(*out.mutable_items());
  out.set_container_guid(virtual_container_->get_container_guid());
}

void user_virtual_inventory::dump(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out) const {
  out.Clear();
  virtual_container_->foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    *out.Add() = inst;
    return true;
  });
}

void user_virtual_inventory::replace(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& items) {
  // 整体替换 = 先清空现有条目, 再逐条载入新列表。
  // 单条载入失败 (数据非法 / 数量超限) 只记日志并跳过, 不影响其余数据落地。
  virtual_container_->clear();

  auto config_group = excel::get_current_config_group();
  for (const auto& item : items) {
    if (!virtual_container_->load(config_group, item, item_algorithm::ItemOperationReason::kReplaceLoad)) {
      FWLOGWARNING("user_virtual_inventory replace skip invalid item, type_id: {}, count: {}",
                   item.item_basic().type_id(), item.item_basic().count());
    }
  }
}
