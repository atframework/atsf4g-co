// Copyright 2026 atframework

#include "logic/item/user_virtual_inventory.h"

#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <config/excel/config_manager.h>
#include <data/user.h>
#include <logic/item/user_item_container_manager.h>

#include <algorithm>
#include <utility>

user_virtual_inventory_container::user_virtual_inventory_container(user* owner)
    : base_type(owner, "Item.UserVirtualInventory") {}
user_virtual_inventory_container::~user_virtual_inventory_container() = default;

void user_virtual_inventory_container::init(int32_t /*row_size*/, int32_t /*column_size*/,
                                            PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                            int64_t container_guid) {
  if (this->is_initialized()) {
    FWLOGERROR("user_virtual_inventory_container::init called twice, container_guid: {}", container_guid);
    return;
  }
  // 无位置模式不落位, 行列参数无意义, 直接忽略
  this->mode_container_type::init(position_type, container_guid);
  this->finish_init();
}

user_virtual_inventory::user_virtual_inventory(user* owner)
    : owner_(owner),
      virtual_container_(atfw::component::memory::stl::make_strong_rc<user_virtual_inventory_container>(owner)) {}

void user_virtual_inventory::init_container() {
  // 虚拟道具仓库只有一个无位置网格, 容器GUID不持久化, 创建时分配
  virtual_container_->init(0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory,
                           owner_->get_user_item_container_manager().allocate_container_guid());
}

void user_virtual_inventory::init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data) {
  virtual_container_->init(0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, data.container_guid());
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
  // check_replace 只引用传入的视图 (且参数是非 const 引用, 临时视图传不进来),
  // 所以这里先落地一个具名视图, 保证它活到 replace 执行完
  auto requests = item_algorithm::make_item_readable_iterable(items);
  auto checked_request = virtual_container_->check_replace(excel::get_current_config_group(), requests);
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWLOGERROR("user_virtual_inventory replace check_replace failed, error: {}({})", checked_request.result.error_code,
               PROJECT_NAMESPACE_ID::EnErrorCode_Name(checked_request.result.error_code));
    return;
  }

  auto result = virtual_container_->replace(checked_request);
  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWLOGERROR("user_virtual_inventory replace failed, error: {}({})", result.error_code,
               PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
  }
}
