// Copyright 2026 atframework

#include "logic/item/user_item_grid_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.user.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <config/excel/config_manager.h>

#include <data/user.h>

#include <algorithm>
#include <map>

namespace {
static bool init_user_item_grid_manager_handle() {
  user::init_get_info_handle(
      PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
          PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserVirtualInventoryFieldNumber),
      [](rpc::context&, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
        user_inst.get_user_item_grid_manager().dump_virtual_inventory(*rsp.mutable_user_virtual_inventory());
      });
  return true;
}
}  // namespace

user_item_grid_manager::user_item_grid_manager(user& owner) : owner_(&owner), virtual_inventory_(&owner) {
  ATFW_EXPLICIT_UNUSED_ATTR static bool init_handle = init_user_item_grid_manager_handle();
}

void user_item_grid_manager::init_from_table_data(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                  const PROJECT_NAMESPACE_ID::table_user& user_table) {
  // 加载分配器
  if (user_table.has_user_item_grid_manager_data()) {
    manager_data_ = user_table.user_item_grid_manager_data();
  } else {
    manager_data_.Clear();
    manager_data_.set_next_container_guid(1);
  }

  virtual_inventory_.init(user_table.virtual_inventory());
}

int user_item_grid_manager::dump(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                 PROJECT_NAMESPACE_ID::table_user& user_table) const {
  *user_table.mutable_user_item_grid_manager_data() = manager_data_;
  dump_virtual_inventory(*user_table.mutable_virtual_inventory());
  return 0;
}

void user_item_grid_manager::dump_virtual_inventory(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const {
  virtual_inventory_.dump(out);
}

void user_item_grid_manager::create_init(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) {
  // 初始化分配器
  if (manager_data_.next_container_guid() <= 0) {
    manager_data_.set_next_container_guid(1);
  }
}

void user_item_grid_manager::login_init(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) {}

int64_t user_item_grid_manager::allocate_container_guid() {
  int64_t guid = static_cast<int64_t>(manager_data_.next_container_guid());
  manager_data_.set_next_container_guid(manager_data_.next_container_guid() + 1);
  return guid;
}

void user_item_grid_manager::on_item_changed(int64_t container_guid, const item_algorithm::item_grid_entry_ptr_t& entry,
                                             item_algorithm::ItemGridOperationReason /*reason*/) {
  if (container_guid == 0 || !entry) {
    return;
  }
  dirty_entries_.insert(std::make_pair(container_guid, entry->entry_id()));

  owner_->insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(this), "user.user_item_grid_manager.dirty", [](gsl::string_view, user&) {
        user::dirty_sync_handle_t handle;
        handle.build_fn = [](rpc::context&, user& user_inst, user::dirty_message_container& output) {
          auto& mgr = user_inst.get_user_item_grid_manager();
          if (mgr.dirty_entries_.empty()) {
            return;
          }
          if (!output.user_dirty) {
            output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
          }
          if (!output.user_dirty) {
            FWLOGERROR("malloc dirty msg body failed");
            return;
          }
          mgr.build_dirty_sync(*output.user_dirty);
        };
        handle.clear_fn = [](rpc::context&, user& user_inst) {
          user_inst.get_user_item_grid_manager().dirty_entries_.clear();
        };
        return handle;
      });
}

void user_item_grid_manager::build_dirty_sync(PROJECT_NAMESPACE_ID::SCUserDirtyChgSync& output) {
  if (dirty_entries_.empty()) {
    return;
  }

  // 按容器 GUID 分组
  std::map<int64_t, std::vector<uint64_t>> container_entries;
  for (const auto& pair : dirty_entries_) {
    container_entries[pair.first].push_back(pair.second);
  }

  for (const auto& container_pair : container_entries) {
    int64_t container_guid = container_pair.first;
    auto grid = find_grid_by_container_guid(container_guid);
    if (grid == nullptr) {
      FWLOGWARNING("user_item_grid_manager build_dirty_sync container not found, container_guid: {}", container_guid);
      continue;
    }

    auto* chg = output.add_dirty_item_chgs();
    chg->set_container_guid(container_guid);

    for (uint64_t entry_id : container_pair.second) {
      auto entry = grid->find_entry_by_id(entry_id);
      if (!entry || entry->item_instance().item_basic().count() <= 0) {
        chg->add_removed_entry_ids(entry_id);
      } else {
        auto* update = chg->add_update_entries();
        update->set_entry_id(entry_id);
        *update->mutable_instance() = entry->item_instance();
      }
    }
  }
}

void user_item_grid_manager::register_item_grid_algorithm(
    atfw::util::memory::strong_rc_ptr<user_item_grid_algorithm> grid) {
  container_guid_to_grid_[grid->get_container_guid()] = grid;
}

void user_item_grid_manager::unregister_item_grid_algorithm(user_item_grid_algorithm* grid) {
  auto iter = container_guid_to_grid_.find(grid->get_container_guid());
  if (iter != container_guid_to_grid_.end()) {
    auto ptr = iter->second.lock().get();
    if (ptr == nullptr) {
      FWLOGERROR("user_item_grid_manager unregister_item_grid_algorithm container_guid: {} grid ptr expired",
                 grid->get_container_guid());
      container_guid_to_grid_.erase(iter);
    } else if (ptr == grid) {
      container_guid_to_grid_.erase(iter);
    }
  }
}

atfw::util::memory::strong_rc_ptr<user_item_grid_algorithm> user_item_grid_manager::find_grid_by_container_guid(
    int64_t container_guid) const {
  auto iter = container_guid_to_grid_.find(container_guid);
  if (iter != container_guid_to_grid_.end()) {
    return iter->second.lock();
  }
  return nullptr;
}

item_algorithm::item_grid_algorithm_ptr_t user_item_grid_manager::select_grid(
    const PROJECT_NAMESPACE_ID::DItemPosition& position) {
  return find_grid_by_container_guid(position.container_guid());
}

item_algorithm::item_grid_algorithm_ptr_t user_item_grid_manager::select_grid(
    const PROJECT_NAMESPACE_ID::DItemPosition& position) const {
  return find_grid_by_container_guid(position.container_guid());
}

item_algorithm::ItemGridContainerAddCheckedRequest user_item_grid_manager::check_add(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    item_algorithm::ItemGridAddRequest&& in_requests) {
  return item_algorithm::ItemGridContainer::check_add(in_config_group, std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::add(
    item_algorithm::ItemGridContainerAddCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::add(checked_request);
}
item_algorithm::ItemGridContainerSubCheckedRequest user_item_grid_manager::check_sub(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    item_algorithm::ItemGridSubRequest&& in_requests) {
  return item_algorithm::ItemGridContainer::check_sub(in_config_group, std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::sub(
    item_algorithm::ItemGridContainerSubCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::sub(checked_request);
}
item_algorithm::ItemGridContainerMoveCheckedRequest user_item_grid_manager::check_move(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    std::vector<item_algorithm::ItemGridContainerMoveRequest>&& in_requests) {
  return item_algorithm::ItemGridContainer::check_move(in_config_group, std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::move(
    item_algorithm::ItemGridContainerMoveCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::move(checked_request);
}
item_algorithm::ItemGridContainerReplaceCheckedRequest user_item_grid_manager::check_replace(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    item_algorithm::ItemGridReplaceRequest&& in_requests) {
  return item_algorithm::ItemGridContainer::check_replace(in_config_group, std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::replace(
    item_algorithm::ItemGridContainerReplaceCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::replace(checked_request);
}
