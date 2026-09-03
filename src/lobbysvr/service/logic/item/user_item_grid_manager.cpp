// Copyright 2026 atframework

#include "logic/item/user_item_grid_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item_type.common.pb.h>
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
#include <utility>
#include <vector>

namespace {
static bool init_user_item_grid_manager_handle() {
  user::init_get_info_handle(
      PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
          PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserVirtualInventoryFieldNumber),
      [](rpc::context&, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
        user_inst.get_user_item_grid_manager().dump_virtual_inventory(*rsp.mutable_user_virtual_inventory());
      });
  PROJECT_NAMESPACE_ID::EnItemType item_types[] = {PROJECT_NAMESPACE_ID::EN_ITEM_TYPE_COIN,
                                                   PROJECT_NAMESPACE_ID::EN_ITEM_TYPE_VIRTUAL};
  user_item_manager::register_item_type_handler(
      item_types, atfw::component::memory::stl::make_strong_rc<user_grid_item_operation_handler>());
  user_item_grid_manager::register_find_position_handle(
      item_types,
      [](rpc::context&, user& user_inst,
         google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& data) -> bool {
        google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> empty;
        google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed_item;
        google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> input;
        input.Swap(&data);
        if (!user_inst.get_user_item_grid_manager()
                 .get_virtual_inventory()
                 .get_virtual_grid()
                 ->find_positions_for_instances(excel::get_current_config_group(), input, empty, data, failed_item)) {
          return false;
        }
        if (!failed_item.empty()) {
          return false;
        }
        return true;
      });
  return true;
}
}  // namespace

user_grid_item_operation_handler::~user_grid_item_operation_handler() {}

item_operation_handle_checked_add_request user_grid_item_operation_handler::check_add(
    rpc::context&, user& user_inst,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&& input) const {
  auto result = user_inst.get_user_item_grid_manager().check_add(std::move(input));
  if (result.get_error_code() != 0) {
    return item_operation_handle_checked_add_request(result.get_error_code(), result.get_failed_index());
  }
  return item_operation_handle_checked_add_request(
      atfw::component::memory::stl::make_strong_rc<user_grid_item_operation_checked_add_data>(std::move(result)));
}

item_operation_handle_checked_sub_request user_grid_item_operation_handler::check_sub(
    rpc::context&, user& user_inst,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&& input) const {
  auto result = user_inst.get_user_item_grid_manager().check_sub(std::move(input));
  if (result.get_error_code() != 0) {
    return item_operation_handle_checked_sub_request(result.get_error_code(), result.get_failed_index());
  }
  return item_operation_handle_checked_sub_request(
      atfw::component::memory::stl::make_strong_rc<user_grid_item_operation_checked_sub_data>(std::move(result)));
}

item_operation_result user_grid_item_operation_handler::check_has(
    rpc::context&, user& user_inst,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&& input) const {
  auto result = user_inst.get_user_item_grid_manager().check_has(std::move(input));
  return {result.error_code, result.failed_index};
}

item_operation_result user_grid_item_operation_handler::add(rpc::context&, user& user_inst,
                                                            item_operation_handle_checked_add_request&& input) {
  auto data_ptr =
      atfw::util::memory::static_pointer_cast<user_grid_item_operation_checked_add_data>(input.checked_request);
  auto result = user_inst.get_user_item_grid_manager().add(data_ptr->data);
  return {result.error_code, result.failed_index};
}

item_operation_result user_grid_item_operation_handler::sub(rpc::context&, user& user_inst,
                                                            item_operation_handle_checked_sub_request&& input) {
  auto data_ptr =
      atfw::util::memory::static_pointer_cast<user_grid_item_operation_checked_sub_data>(input.checked_request);
  auto result = user_inst.get_user_item_grid_manager().sub(data_ptr->data);
  return {result.error_code, result.failed_index};
}

bool user_grid_item_operation_handler::find_position(
    rpc::context& ctx, user& user_inst, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& data) {
  return user_inst.get_user_item_grid_manager().find_position(ctx, data);
}

std::unordered_map<PROJECT_NAMESPACE_ID::EnItemType, int32_t> user_item_grid_manager::find_position_handle_id_;
std::unordered_map<int32_t, user_item_grid_manager::find_position_handle_t>
    user_item_grid_manager::find_position_handle;

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

void user_item_grid_manager::register_find_position_handle(gsl::span<const PROJECT_NAMESPACE_ID::EnItemType> item_type,
                                                           find_position_handle_t handle) {
  static uint32_t handle_id = 10000;
  ++handle_id;
  if (item_type.empty()) {
    FWLOGERROR("item_type span is empty");
    abort();
  }
  if (!handle) {
    FWLOGERROR("find_position_handle is empty");
    abort();
  }
  for (auto type : item_type) {
    if (find_position_handle_id_.count(type)) {
      FWLOGERROR("find_position_handle_id_ already contains type: {}", static_cast<int>(type));
      abort();
    }
    find_position_handle_id_[type] = handle_id;
  }
  find_position_handle[handle_id] = handle;
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
    item_algorithm::ItemGridAddRequest&& in_requests) const {
  return item_algorithm::ItemGridContainer::check_add(excel::get_current_config_group(), std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::add(
    item_algorithm::ItemGridContainerAddCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::add(checked_request);
}
item_algorithm::ItemGridContainerSubCheckedRequest user_item_grid_manager::check_sub(
    item_algorithm::ItemGridSubRequest&& in_requests) const {
  return item_algorithm::ItemGridContainer::check_sub(excel::get_current_config_group(), std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::sub(
    item_algorithm::ItemGridContainerSubCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::sub(checked_request);
}
item_algorithm::ItemGridContainerMoveCheckedRequest user_item_grid_manager::check_move(
    std::vector<item_algorithm::ItemGridContainerMoveRequest>&& in_requests) const {
  return item_algorithm::ItemGridContainer::check_move(excel::get_current_config_group(), std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::move(
    item_algorithm::ItemGridContainerMoveCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::move(checked_request);
}
item_algorithm::ItemGridContainerReplaceCheckedRequest user_item_grid_manager::check_replace(
    item_algorithm::ItemGridReplaceRequest&& in_requests) const {
  return item_algorithm::ItemGridContainer::check_replace(excel::get_current_config_group(), std::move(in_requests));
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::replace(
    item_algorithm::ItemGridContainerReplaceCheckedRequest& checked_request) {
  return item_algorithm::ItemGridContainer::replace(checked_request);
}
item_algorithm::ItemGridOperationResult user_item_grid_manager::check_has(
    const item_algorithm::ItemGridHasRequest& requests) const {
  return item_algorithm::ItemGridContainer::check_has(excel::get_current_config_group(), requests);
}

bool user_item_grid_manager::find_position(
    rpc::context& ctx, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& data) const {
  // 通过道具ID分到 handler id
  std::map<uint32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>> handler_map;
  for (auto& item_instance : data) {
    auto type_config = ItemAlgorithmTypeOption::GetItemType(static_cast<int32_t>(item_instance.item_basic().type_id()));
    if (type_config == nullptr) {
      return false;
    }
    auto iter = find_position_handle_id_.find(type_config->item_type);
    if (iter == find_position_handle_id_.end()) {
      FWLOGERROR("Failed to find handler id for type_id: {}", item_instance.item_basic().type_id());
      return false;
    }
    uint32_t handler_id = iter->second;
    protobuf_move_message((*handler_map[handler_id].Add()), std::move(item_instance));
  }
  data.Clear();
  // 通过handler id 分发
  for (auto& pair : handler_map) {
    if (!find_position_handle[pair.first](ctx, *owner_, pair.second)) {
      return false;
    }
    for (auto& item_instance : pair.second) {
      protobuf_move_message((*data.Add()), std::move(item_instance));
    }
  }
  return true;
}
