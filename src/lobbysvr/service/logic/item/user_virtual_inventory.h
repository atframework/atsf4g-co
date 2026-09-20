// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>

#include <logic/item/user_item_container.h>

class user;

/// @brief 虚拟道具仓库容器: 只按道具类型记录数量 (无位置模式)
class user_virtual_inventory_container : public user_item_no_position_container {
 public:
  using user_item_no_position_container::user_item_no_position_container;
};

class user_virtual_inventory {
 public:
  explicit user_virtual_inventory(user* owner);

  // 分配容器GUID并初始化容器。容器GUID不持久化
  void init_container();
  void init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data);
  void dump(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const;
  void dump(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out) const;
  // 全量替换: 清空现有条目后放入新列表
  void replace(const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& items);

  atfw::util::memory::strong_rc_ptr<user_virtual_inventory_container> get_virtual_container() const {
    return virtual_container_;
  }

 private:
  user* owner_ = nullptr;
  atfw::util::memory::strong_rc_ptr<user_virtual_inventory_container> virtual_container_;
};
