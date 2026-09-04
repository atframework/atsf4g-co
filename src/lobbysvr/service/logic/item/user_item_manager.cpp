// Copyright 2026 atframework

#include "logic/item/user_item_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.user.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

#include <ItemInitialize/ItemInitialize.h>
#include <logic/item/user_item_grid_manager.h>
#include <rpc/db/uuid.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/user.h>

#include <algorithm>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

std::unordered_map<PROJECT_NAMESPACE_ID::EnItemType, int32_t> user_item_manager::item_type_handler_id_;
std::unordered_map<int32_t, atfw::util::memory::strong_rc_ptr<item_operation_handler>>
    user_item_manager::item_type_handler_;

user_item_manager::user_item_manager(user& owner) : owner_(&owner) {}

void user_item_manager::register_item_type_handler(gsl::span<const PROJECT_NAMESPACE_ID::EnItemType> item_type,
                                                   atfw::util::memory::strong_rc_ptr<item_operation_handler> handler) {
  if (item_type.empty()) {
    FWLOGERROR("user_item_manager::register_item_type_handler: item_type is empty");
    abort();  // 空的道具类型列表
  }
  if (handler == nullptr) {
    FWLOGERROR("user_item_manager::register_item_type_handler: handler is nullptr");
    abort();  // 空的处理器
  }
  static int32_t handler_id_ = 10000;
  handler_id_++;
  item_type_handler_[handler_id_] = handler;
  for (auto item_type_value : item_type) {
    if (item_type_handler_id_.count(item_type_value)) {
      FWLOGERROR("user_item_manager::register_item_type_handler: item_type {} already registered",
                 static_cast<int32_t>(item_type_value));
      abort();  // 重复注册
    }
    item_type_handler_id_[item_type_value] = handler_id_;
  }
}

item_operation_result item_operation_checked_add_request::do_operation(rpc::context& ctx) {
  if (check_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return check_result;
  }
  for (auto& group : checked_request) {
    auto handler_it = user_item_manager::item_type_handler_.find(group.first);
    if (handler_it == user_item_manager::item_type_handler_.end()) {
      return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_HANDLE_NOT_FOUND, -1};
    }
    if (group.second.checked_request == nullptr) {
      return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN, -1};
    }
    auto& handler = handler_it->second;
    auto result = handler->add(ctx, *owner_, std::move(group.second));
    if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return result;
    }
  }
  return {};
}
item_operation_result item_operation_checked_sub_request::do_operation(rpc::context& ctx) {
  if (check_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return check_result;
  }
  for (auto& group : checked_request) {
    auto handler_it = user_item_manager::item_type_handler_.find(group.first);
    if (handler_it == user_item_manager::item_type_handler_.end()) {
      return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_HANDLE_NOT_FOUND, -1};
    }
    if (group.second.checked_request == nullptr) {
      return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN, -1};
    }
    auto& handler = handler_it->second;
    auto result = handler->sub(ctx, *owner_, std::move(group.second));
    if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return result;
    }
  }
  return {};
}

item_operation_checked_add_request user_item_manager::check_add(
    rpc::context& ctx, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&& input) const {
  // 首先分组
  std::unordered_map<int32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>> grouped_requests;
  int32_t index = 0;
  std::list<std::pair<int32_t, item_operation_handle_checked_add_request>> checked_request;
  for (auto& item : input) {
    auto type_config = ItemAlgorithmTypeOption::GetItemType(static_cast<int32_t>(item.item_basic().type_id()));
    if (type_config == nullptr) {
      return item_operation_checked_add_request(owner_, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_NOT_FOUND, index);
    }
    auto handler_id_it = item_type_handler_id_.find(type_config->item_type);
    if (handler_id_it == item_type_handler_id_.end()) {
      return item_operation_checked_add_request(owner_, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_HANDLE_NOT_FOUND, index);
    }
    grouped_requests[handler_id_it->second].Add(std::move(item));
    index++;
  }
  for (auto& group : grouped_requests) {
    auto& handler = item_type_handler_.find(group.first)->second;
    // 调用具体的处理器进行检查
    auto result = handler->check_add(ctx, *owner_, std::move(group.second));
    if (result.check_result.error_code != 0) {
      return item_operation_checked_add_request(owner_, result.check_result.error_code,
                                                result.check_result.failed_index);
    }
    checked_request.push_back(std::make_pair(group.first, std::move(result)));
  }
  return item_operation_checked_add_request(owner_, std::move(checked_request));
}

item_operation_checked_sub_request user_item_manager::check_sub(
    rpc::context& ctx, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&& input) const {
  // 首先分组
  std::unordered_map<int32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>> grouped_requests;
  int32_t index = 0;
  std::list<std::pair<int32_t, item_operation_handle_checked_sub_request>> checked_request;
  for (auto& item : input) {
    auto type_config = ItemAlgorithmTypeOption::GetItemType(static_cast<int32_t>(item.type_id()));
    if (type_config == nullptr) {
      return item_operation_checked_sub_request(owner_, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_NOT_FOUND, index);
    }
    auto handler_id_it = item_type_handler_id_.find(type_config->item_type);
    if (handler_id_it == item_type_handler_id_.end()) {
      return item_operation_checked_sub_request(owner_, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_HANDLE_NOT_FOUND, index);
    }
    grouped_requests[handler_id_it->second].Add(std::move(item));
    index++;
  }
  for (auto& group : grouped_requests) {
    auto& handler = item_type_handler_.find(group.first)->second;
    // 调用具体的处理器进行检查
    auto result = handler->check_sub(ctx, *owner_, std::move(group.second));
    if (result.check_result.error_code != 0) {
      return item_operation_checked_sub_request(owner_, result.check_result.error_code,
                                                result.check_result.failed_index);
    }
    checked_request.push_back(std::make_pair(group.first, std::move(result)));
  }
  return item_operation_checked_sub_request(owner_, std::move(checked_request));
}

item_operation_checked_sub_request user_item_manager::check_sub(
    rpc::context& ctx, const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>& input,
    int32_t multiple) const {
  if (multiple <= 0) {
    return item_operation_checked_sub_request(owner_, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, -1);
  }
  // 只支持虚拟仓库
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basic_input;
  for (auto& item : input) {
    if (ItemAlgorithmTypeOption::IsNeedOccupyTheGrid(static_cast<int32_t>(item.item_offset().type_id()))) {
      return item_operation_checked_sub_request(owner_, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, -1);
    }
    auto& basic = *basic_input.Add();
    basic.set_type_id(item.item_offset().type_id());
    basic.set_count(item.item_offset().count() * multiple);
    basic.mutable_position()->set_container_guid(
        owner_->get_user_item_grid_manager().get_virtual_inventory_container_guid());
    basic.mutable_position()->mutable_grid_position()->set_virtual_inventory(true);
  }
  return check_sub(ctx, std::move(basic_input));
}

item_operation_result user_item_manager::check_has(
    rpc::context& ctx, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&& input) {
  // 首先分组
  std::unordered_map<int32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>> grouped_requests;
  int32_t index = 0;
  for (auto& item : input) {
    auto type_config = ItemAlgorithmTypeOption::GetItemType(static_cast<int32_t>(item.type_id()));
    if (type_config == nullptr) {
      return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_NOT_FOUND, index};
    }
    auto handler_id_it = item_type_handler_id_.find(type_config->item_type);
    if (handler_id_it == item_type_handler_id_.end()) {
      return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_ITEM_TYPE_HANDLE_NOT_FOUND, index};
    }
    grouped_requests[handler_id_it->second].Add(std::move(item));
    index++;
  }
  for (auto& group : grouped_requests) {
    auto& handler = item_type_handler_.find(group.first)->second;
    // 调用具体的处理器进行检查
    auto result = handler->check_has(ctx, *owner_, std::move(group.second));
    if (result.error_code != 0) {
      return result;
    }
  }
  return item_operation_result{PROJECT_NAMESPACE_ID::EN_SUCCESS, -1};
}

rpc::result_code_type user_item_manager::generate_item_from_offset_cfg(
    rpc::context& ctx, const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>& offset_cfg,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out_instances, int32_t multiple) const {
  if (multiple <= 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  out_instances.Clear();
  // 生成物品
  for (auto& offset : offset_cfg) {
    // 初始化道具实例
    PROJECT_NAMESPACE_ID::battle_utility::item_initialize::ItemInitializeArgs args;
    if (!PROJECT_NAMESPACE_ID::battle_utility::item_initialize::CreateItem(
            excel::get_current_config_group(), PROJECT_NAMESPACE_ID::battle_utility::random::GetRandomGenerator(),
            offset.item_offset().type_id(), offset.item_offset().count() * multiple, args, out_instances)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    }
  }
  // 生成guid
  for (auto& item : out_instances) {
    if (ItemAlgorithmTypeOption::IsNeedGuid(item.item_basic().type_id())) {
      int64_t guid = RPC_AWAIT_CODE_RESULT(generate_item_guid(ctx));
      if (guid <= 0) {
        RPC_RETURN_CODE(static_cast<int32_t>(guid));
      }
      item.mutable_item_basic()->set_guid(guid);
    }
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
}

bool user_item_manager::find_position(rpc::context& ctx,
                                      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& input) {
  std::unordered_map<int32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>> grouped_requests;
  std::list<std::pair<int32_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>>> request;
  for (auto& item : input) {
    auto type_config = ItemAlgorithmTypeOption::GetItemType(static_cast<int32_t>(item.item_basic().type_id()));
    if (type_config == nullptr) {
      return false;
    }
    auto handler_id_it = item_type_handler_id_.find(type_config->item_type);
    if (handler_id_it == item_type_handler_id_.end()) {
      return false;
    }
    grouped_requests[handler_id_it->second].Add(std::move(item));
  }
  input.Clear();
  for (auto& group : grouped_requests) {
    auto& handler = item_type_handler_.find(group.first)->second;
    // 调用具体的处理器进行检查
    if (!handler->find_position(ctx, *owner_, group.second)) {
      return false;
    }
    for (auto& item : group.second) {
      protobuf_move_message(*input.Add(), std::move(item));
    }
  }
  return true;
}

rpc::rpc_result<int64_t> user_item_manager::generate_item_guid(rpc::context& ctx) const {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::uuid::generate_global_unique_id(
      ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_ITEM_GUID, owner_->get_zone_id(), 0)));
}

bool user_item_manager::check_offset_instance_match(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>& offset_cfg,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& instances, int32_t multiple) {
  if (multiple <= 0) {
    return false;
  }
  // 仅检查数量
  std::unordered_map<int32_t, int64_t> offset_count;
  for (auto& offset : offset_cfg) {
    offset_count[offset.item_offset().type_id()] += offset.item_offset().count() * multiple;
  }
  for (auto& instance : instances) {
    if (instance.item_basic().count() <= 0) {
      return false;
    }
    auto& count = offset_count[instance.item_basic().type_id()];
    count -= instance.item_basic().count();
    if (count < 0) {
      return false;
    }
    if (count == 0) {
      offset_count.erase(instance.item_basic().type_id());
    }
  }
  return offset_count.empty();
}
