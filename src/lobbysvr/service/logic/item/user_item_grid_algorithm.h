// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>

#include <ItemAlgorithm/ItemGridAlgorithm.h>
#include <ItemAlgorithm/ItemGridContainer.h>
#include <ItemAlgorithm/ItemGridData.h>

#include <functional>
#include <string>

class user;

namespace item_algorithm = ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm;

class user_item_grid_algorithm : public item_algorithm::ItemGridAlgorithm {
 public:
  using dirty_notify_fn_t =
      std::function<void(int64_t container_guid, const item_algorithm::item_grid_entry_ptr_t& entry,
                         item_algorithm::ItemGridOperationReason reason)>;

  user_item_grid_algorithm(user* owner, const std::string& log_category_prefix);
  ~user_item_grid_algorithm() override;

  using item_algorithm::ItemGridAlgorithm::init;

  virtual void init(int32_t row_size, int32_t column_size,
                    PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid);
  user* get_owner() { return owner_; }
  user* get_owner() const { return const_cast<user*>(owner_); }
  void destroy();

 protected:
  void on_item_data_changed(const item_algorithm::item_grid_entry_ptr_t& entry,
                            item_algorithm::ItemGridOperationReason reason) override;

 private:
  user* owner_ = nullptr;
  dirty_notify_fn_t dirty_notify_fn_;
  std::string log_category_prefix_;
  bool register_grid_ = false;
  bool init_ = false;
};
