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

#include <logic/item/user_item_grid_algorithm.h>

class user;

class user_virtual_inventory_grid : public user_item_grid_algorithm {
 public:
  explicit user_virtual_inventory_grid(user* owner);
  ~user_virtual_inventory_grid() override;

  item_algorithm::item_grid_algorithm_ptr_t create_empty_clone() const override;
};

class user_virtual_inventory {
 public:
  explicit user_virtual_inventory(user* owner);

  // 分配容器GUID并初始化网格。容器GUID不持久化
  void init_grid();
  void init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data);
  void dump(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const;
  void dump(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out) const;
  // 全量替换: 清空现有条目后放入新列表
  void replace(const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& items);

  atfw::util::memory::strong_rc_ptr<user_virtual_inventory_grid> get_virtual_grid() const { return virtual_grid_; }

 private:
  user* owner_ = nullptr;
  atfw::util::memory::strong_rc_ptr<user_virtual_inventory_grid> virtual_grid_;
};
