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
  user_virtual_inventory_grid(user* owner);
  ~user_virtual_inventory_grid() override;

  virtual item_algorithm::item_grid_algorithm_ptr_t create_empty_clone() const override;
};

class user_virtual_inventory {
 public:
  user_virtual_inventory(user* owner);

  void init(const PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& data);
  void dump(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const;

  atfw::util::memory::strong_rc_ptr<user_virtual_inventory_grid> get_virtual_grid() const { return virtual_grid_; }

 private:
  user* owner_ = nullptr;
  atfw::util::memory::strong_rc_ptr<user_virtual_inventory_grid> virtual_grid_;
};