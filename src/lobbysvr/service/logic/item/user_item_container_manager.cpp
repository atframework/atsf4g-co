// Copyright 2026 atframework

#include "logic/item/user_item_container_manager.h"

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
#include <config/excel_config_const_index.h>

#include <data/user.h>

#include <unordered_map>
#include <utility>
#include <vector>

namespace {
static bool init_user_item_container_manager_handle() {
  user::init_get_info_handle(
      PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
          PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserVirtualInventoryFieldNumber),
      [](rpc::context&, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
        user_inst.get_user_item_container_manager().dump_virtual_inventory(*rsp.mutable_user_virtual_inventory());
      });
  PROJECT_NAMESPACE_ID::EnItemType item_types[] = {PROJECT_NAMESPACE_ID::EN_ITEM_TYPE_COIN,
                                                   PROJECT_NAMESPACE_ID::EN_ITEM_TYPE_VIRTUAL};
  user_item_manager::register_item_type_handler(
      item_types, atfw::component::memory::stl::make_strong_rc<user_container_item_operation_handler>());
  user_item_container_manager::register_find_position_handle(
      item_types,
      [](rpc::context&, user& user_inst,
         google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& data) -> bool {
        google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> empty;
        google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed_item;
        google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> input;
        input.Swap(&data);
        if (!user_inst.get_user_item_container_manager()
                 .get_virtual_inventory()
                 .get_virtual_container()
                 ->find_positions_for_instances(
                     excel::get_current_config_group(), item_algorithm::make_item_readable_iterable(input),
                     item_algorithm::make_item_readable_iterable(empty), data, failed_item)) {
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

user_container_item_operation_handler::~user_container_item_operation_handler() {}

item_operation_handle_checked_add_request user_container_item_operation_handler::check_add(
    rpc::context&, user& user_inst,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&& input) const {
  // input 是本函数持有的数据, 视图在 check_add 期间一直有效 (组会把请求拷进各容器分片)
  auto result =
      user_inst.get_user_item_container_manager().check_add(item_algorithm::make_item_readable_iterable(input));
  if (result.get_error_code() != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return item_operation_handle_checked_add_request(result.get_error_code(), result.get_failed_type_id());
  }
  return item_operation_handle_checked_add_request(
      atfw::component::memory::stl::make_strong_rc<user_container_item_operation_checked_add_data>(std::move(result)));
}

item_operation_handle_checked_sub_request user_container_item_operation_handler::check_sub(
    rpc::context&, user& user_inst,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&& input) const {
  // input 是本函数持有的数据, 视图在 check_sub 期间一直有效 (组会把请求拷进各容器分片)
  auto result =
      user_inst.get_user_item_container_manager().check_sub(item_algorithm::make_item_readable_iterable(input));
  if (result.get_error_code() != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return item_operation_handle_checked_sub_request(result.get_error_code(), result.get_failed_type_id());
  }
  return item_operation_handle_checked_sub_request(
      atfw::component::memory::stl::make_strong_rc<user_container_item_operation_checked_sub_data>(std::move(result)));
}

item_operation_result user_container_item_operation_handler::check_has(
    rpc::context&, user& user_inst,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&& input) const {
  // input 是本函数持有的数据, 视图在 check_has 期间一直有效
  auto result =
      user_inst.get_user_item_container_manager().check_has(item_algorithm::make_item_readable_iterable(input));
  return {result.error_code, result.failed_type_id};
}

item_operation_result user_container_item_operation_handler::add(rpc::context&, user& user_inst,
                                                                 item_operation_handle_checked_add_request&& input) {
  auto data_ptr =
      atfw::util::memory::static_pointer_cast<user_container_item_operation_checked_add_data>(input.checked_request);
  auto result = user_inst.get_user_item_container_manager().add(data_ptr->data);
  return {result.error_code, result.failed_type_id};
}

item_operation_result user_container_item_operation_handler::sub(rpc::context&, user& user_inst,
                                                                 item_operation_handle_checked_sub_request&& input) {
  auto data_ptr =
      atfw::util::memory::static_pointer_cast<user_container_item_operation_checked_sub_data>(input.checked_request);
  auto result = user_inst.get_user_item_container_manager().sub(data_ptr->data);
  return {result.error_code, result.failed_type_id};
}

int32_t user_container_item_operation_handler::on_item_not_enough(rpc::context& ctx, user& user_inst,
                                                                  int32_t type_id) const {
  return user_inst.get_user_item_container_manager().get_item_not_enough_error_code(ctx, type_id);
}

int64_t user_container_item_operation_handler::get_count(rpc::context& ctx, user& user_inst, int32_t type_id) const {
  return user_inst.get_user_item_container_manager().get_count(ctx, type_id);
}

bool user_container_item_operation_handler::find_position(
    rpc::context& ctx, user& user_inst, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& data) {
  return user_inst.get_user_item_container_manager().find_position(ctx, data);
}

std::unordered_map<PROJECT_NAMESPACE_ID::EnItemType, int32_t> user_item_container_manager::find_position_handle_id_;
std::unordered_map<int32_t, user_item_container_manager::find_position_handle_t>
    user_item_container_manager::find_position_handle;

user_item_container_manager::user_item_container_manager(user& owner) : owner_(&owner), virtual_inventory_(&owner) {
  ATFW_EXPLICIT_UNUSED_ATTR static bool init_handle = init_user_item_container_manager_handle();
}

void user_item_container_manager::init_from_table_data(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                       const PROJECT_NAMESPACE_ID::table_user& user_table) {
  // 加载分配器
  if (user_table.has_user_item_container_manager_data()) {
    manager_data_ = user_table.user_item_container_manager_data();
  } else {
    manager_data_.Clear();
    manager_data_.set_next_container_guid(1);
  }

  virtual_inventory_.init(user_table.virtual_inventory());
}

int user_item_container_manager::dump(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                      PROJECT_NAMESPACE_ID::table_user& user_table) const {
  *user_table.mutable_user_item_container_manager_data() = manager_data_;
  dump_virtual_inventory(*user_table.mutable_virtual_inventory());
  return 0;
}

void user_item_container_manager::dump_virtual_inventory(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const {
  virtual_inventory_.dump(out);
}

void user_item_container_manager::dump_virtual_inventory(
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out) const {
  virtual_inventory_.dump(out);
}

rpc::result_void_type user_item_container_manager::create_init(rpc::context& ctx) {
  // 初始化分配器
  if (manager_data_.next_container_guid() <= 0) {
    manager_data_.set_next_container_guid(1);
  }
  // 初始化虚拟道具仓库网格
  virtual_inventory_.init_container();
  // 初始化默认道具
  if (!excel::get_const_config().creat_init_item().empty()) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> item_instances;
    int32_t result = RPC_AWAIT_CODE_RESULT(owner_->get_user_item_manager().generate_item_from_offset_cfg(
        ctx, excel::get_const_config().creat_init_item(), item_instances));
    if (result != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
      FWLOGERROR("{} user_item_container_manager::create_init failed, result={}({})", *owner_, result,
                 protobuf_mini_dumper_get_error_msg(result));
      RPC_RETURN_VOID;
    }
    if (item_instances.empty()) {
      RPC_RETURN_VOID;
    }

    const int32_t instance_count = static_cast<int32_t>(item_instances.size());
    if (!owner_->get_user_item_manager().find_position(ctx, item_instances)) {
      // find_position 返回 false 表示道具未配置或没有可用位置
      FWLOGERROR("{} user_item_container_manager::create_init add_item failed to find position, instance_count={}",
                 *owner_, instance_count);
      RPC_RETURN_VOID;
    }

    int32_t item_size = static_cast<int32_t>(item_instances.size());

    auto checked_request = owner_->get_user_item_manager().check_add(ctx, std::move(item_instances));
    auto add_result = checked_request.do_operation(ctx);
    if (add_result.error_code != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
      FWLOGERROR("{} user_item_container_manager::create_init add_item failed, result={}({}), failed_type_id={}",
                 *owner_, add_result.error_code, protobuf_mini_dumper_get_error_msg(add_result.error_code),
                 add_result.failed_type_id);
      RPC_RETURN_VOID;
    }

    FWLOGDEBUG("{} user_item_container_manager::create_init add_item finish, request_count={}, instance_count={}",
               *owner_, item_size, instance_count);
  }
  RPC_RETURN_VOID;
}

void user_item_container_manager::register_find_position_handle(
    gsl::span<const PROJECT_NAMESPACE_ID::EnItemType> item_type, find_position_handle_t handle) {
  static int32_t handle_id = 10000;
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

void user_item_container_manager::login_init(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) {}

int64_t user_item_container_manager::allocate_container_guid() {
  int64_t guid = static_cast<int64_t>(manager_data_.next_container_guid());
  manager_data_.set_next_container_guid(manager_data_.next_container_guid() + 1);
  return guid;
}

void user_item_container_manager::on_item_changed(int64_t container_guid, const item_algorithm::item_entry_ptr_t& entry,
                                                  const item_algorithm::ItemOperationContext& /*context*/) {
  if (container_guid == 0 || !entry) {
    return;
  }
  dirty_entries_.insert(std::make_pair(container_guid, entry->entry_id()));

  owner_->insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(this), "user.user_item_container_manager.dirty", [](gsl::string_view, user&) {
        user::dirty_sync_handle_t handle;
        handle.build_fn = [](rpc::context&, user& user_inst, user::dirty_message_container& output) {
          auto& mgr = user_inst.get_user_item_container_manager();
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
          user_inst.get_user_item_container_manager().dirty_entries_.clear();
        };
        return handle;
      });
}

void user_item_container_manager::on_item_count_changed(int32_t type_id, int64_t delta_count) {
  item_type_to_count_cache_[type_id] += delta_count;
}

void user_item_container_manager::build_dirty_sync(PROJECT_NAMESPACE_ID::SCUserDirtyChgSync& output) {
  if (dirty_entries_.empty()) {
    return;
  }

  // 按容器 GUID 分组
  std::unordered_map<int64_t, std::vector<uint64_t>> container_entries;
  for (const auto& pair : dirty_entries_) {
    container_entries[pair.first].push_back(pair.second);
  }

  for (const auto& container_pair : container_entries) {
    int64_t container_guid = container_pair.first;
    auto container = find_item_container(container_guid);
    if (container == nullptr) {
      FWLOGWARNING("user_item_container_manager build_dirty_sync container not found, container_guid: {}",
                   container_guid);
      continue;
    }

    auto* chg = output.add_dirty_item_chgs();
    chg->set_container_guid(container_guid);

    for (uint64_t entry_id : container_pair.second) {
      auto entry = container->find_entry_by_id(entry_id);
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

void user_item_container_manager::register_item_container(item_algorithm::item_container_ptr_t container) {
  if (container == nullptr) {
    return;
  }
  container_guid_to_container_[container->get_container_guid()] = container;
}

void user_item_container_manager::unregister_item_container(item_algorithm::ItemContainer* container) {
  if (container == nullptr) {
    return;
  }

  auto iter = container_guid_to_container_.find(container->get_container_guid());
  if (iter == container_guid_to_container_.end()) {
    return;
  }

  auto locked = iter->second.lock();
  if (locked == nullptr) {
    FWLOGERROR("user_item_container_manager unregister_item_container container_guid: {} container ptr expired",
               container->get_container_guid());
    container_guid_to_container_.erase(iter);
  } else if (locked.get() == container) {
    container_guid_to_container_.erase(iter);
  }
}

item_algorithm::item_container_ptr_t user_item_container_manager::find_item_container(int64_t container_guid) const {
  auto iter = container_guid_to_container_.find(container_guid);
  if (iter != container_guid_to_container_.end()) {
    return iter->second.lock();
  }
  return nullptr;
}

// ============================================================
// 组的路由 — 按 position.container_guid 找登记的容器
//
// 分片与批量执行都在 SDK 的 ItemContainerGroup 里, 本层只提供路由表:
// 组先按 position 把请求分给容器, 再逐容器 check_* / add / sub / move。
// ============================================================

item_algorithm::item_container_ptr_t user_item_container_manager::select_container(
    const PROJECT_NAMESPACE_ID::DItemPosition& position) {
  return find_item_container(position.container_guid());
}

item_algorithm::item_container_ptr_t user_item_container_manager::select_container(
    const PROJECT_NAMESPACE_ID::DItemPosition& position) const {
  return find_item_container(position.container_guid());
}

// ============================================================
// 组级操作 — 这里只补上配置组与操作来源, 其余交给 ItemContainerGroup
// ============================================================

item_algorithm::ItemContainerGroupAddCheckedRequest user_item_container_manager::check_add(
    const item_algorithm::item_instance_readable_iterable& requests,
    const item_algorithm::ItemOperationSource& source) const {
  return item_algorithm::ItemContainerGroup::check_add(excel::get_current_config_group(), requests, source);
}

item_algorithm::ItemOperationResult user_item_container_manager::add(
    item_algorithm::ItemContainerGroupAddCheckedRequest& checked_request) {
  return item_algorithm::ItemContainerGroup::add(checked_request);
}

item_algorithm::ItemContainerGroupSubCheckedRequest user_item_container_manager::check_sub(
    const item_algorithm::item_basic_readable_iterable& requests,
    const item_algorithm::ItemOperationSource& source) const {
  return item_algorithm::ItemContainerGroup::check_sub(excel::get_current_config_group(), requests, source);
}

item_algorithm::ItemOperationResult user_item_container_manager::sub(
    item_algorithm::ItemContainerGroupSubCheckedRequest& checked_request) {
  return item_algorithm::ItemContainerGroup::sub(checked_request);
}

item_algorithm::ItemContainerGroupMoveCheckedRequest user_item_container_manager::check_move(
    std::vector<item_algorithm::ItemContainerGroupMoveRequest>&& requests,
    const item_algorithm::ItemOperationSource& source) const {
  return item_algorithm::ItemContainerGroup::check_move(excel::get_current_config_group(), std::move(requests), source);
}

item_algorithm::ItemOperationResult user_item_container_manager::move(
    item_algorithm::ItemContainerGroupMoveCheckedRequest& checked_request) {
  return item_algorithm::ItemContainerGroup::move(checked_request);
}

item_algorithm::ItemOperationResult user_item_container_manager::check_has(
    const item_algorithm::item_basic_readable_iterable& requests) const {
  return item_algorithm::ItemContainerGroup::check_has(excel::get_current_config_group(), requests);
}

bool user_item_container_manager::find_position(
    rpc::context& ctx, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& data) const {
  // 通过道具ID分到 handler id
  std::unordered_map<int32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>> handler_map;
  for (auto& item_instance : data) {
    const auto* type_config =
        ItemAlgorithmTypeOption::GetItemType(static_cast<int32_t>(item_instance.item_basic().type_id()));
    if (type_config == nullptr) {
      return false;
    }
    auto iter = find_position_handle_id_.find(type_config->item_type);
    if (iter == find_position_handle_id_.end()) {
      FWLOGERROR("Failed to find handler id for type_id: {}", item_instance.item_basic().type_id());
      return false;
    }
    int32_t handler_id = iter->second;
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

int32_t user_item_container_manager::get_item_not_enough_error_code(rpc::context&, int32_t /*type_id*/) const {
  return PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
}

int64_t user_item_container_manager::get_count(rpc::context&, int32_t type_id) const {
  auto iter = item_type_to_count_cache_.find(type_id);
  if (iter == item_type_to_count_cache_.end()) {
    return 0;
  }
  return iter->second;
}
