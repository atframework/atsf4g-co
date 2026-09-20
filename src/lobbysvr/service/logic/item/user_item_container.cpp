// Copyright 2026 atframework

#include "logic/item/user_item_container.h"

#include <log/log_wrapper.h>
#include <memory/object_allocator.h>

#include <data/user.h>
#include <logic/item/user_item_container_manager.h>

#include <string>
#include <utility>

// ============================================================
// user_item_container — 业务容器公共部分
// ============================================================

template <typename ModeContainerT, typename DerivedT>
user_item_container<ModeContainerT, DerivedT>::user_item_container(user* owner, const std::string& log_category_prefix)
    : owner_(owner), log_category_prefix_(log_category_prefix) {
  item_algorithm::ItemLogHandler handler;
  // SDK 直接给出框架日志级别与调用点, 这里只负责转给本服务的日志分类
  handler.on_log = [this](const item_algorithm::ItemLogRecord& record) {
    using log_wrapper_t = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_wrapper;
    if (!log_wrapper_t::check_level(WDTLOGGETCAT(log_wrapper_t::categorize_t::DEFAULT), record.level)) {
      return;
    }
    WDTLOGGETCAT(log_wrapper_t::categorize_t::DEFAULT)
        ->format_log(log_wrapper_t::caller_info_t(record.level, {}, record.file_path, record.line_number, ""),
                     "[{}][{}] {}", *owner_, record.category, record.message);
  };
  this->set_log_handler(handler);
}

template <typename ModeContainerT, typename DerivedT>
user_item_container<ModeContainerT, DerivedT>::~user_item_container() {}

template <typename ModeContainerT, typename DerivedT>
void user_item_container<ModeContainerT, DerivedT>::finish_init() {
  if (registered_) {
    return;
  }
  registered_ = true;

  item_algorithm::ItemLogHandler handler = this->get_log_handler();
  if (log_category_prefix_.empty()) {
    // category 是视图, 拷成 std::string 自己持有
    log_category_prefix_.assign(handler.category.data(), handler.category.size());
  }

  // 视图不能指向临时量: 先落到局部 string, 在本函数内一直有效,
  // set_log_handler 会把它复制到容器自己的存储
  std::string category = log_category_prefix_ + ":" + std::to_string(this->get_container_guid());
  handler.category = category;
  this->set_log_handler(handler);

  // 容器由 SDK 基类 (ItemContainer) 的共享引用持有, 这里把基类指针登记到管理器:
  // 组按 position.container_guid 路由时用它选容器。
  owner_->get_user_item_container_manager().register_item_container(this->shared_from_this());
}

template <typename ModeContainerT, typename DerivedT>
void user_item_container<ModeContainerT, DerivedT>::destroy() {
  if (registered_) {
    owner_->get_user_item_container_manager().unregister_item_container(this);
    registered_ = false;
  }
}

template <typename ModeContainerT, typename DerivedT>
item_algorithm::ItemGridPosition user_item_container<ModeContainerT, DerivedT>::extract_position(
    const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
  item_algorithm::ItemGridPosition result;
  switch (position.position_type_case()) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory:
      result.x = position.user_inventory().x();
      result.y = position.user_inventory().y();
      break;
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory:
      result.x = position.character_inventory().x();
      result.y = position.character_inventory().y();
      break;
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment:
      // 装备槽: slot_idx 当 x, y 恒为 0
      result.x = static_cast<int32_t>(position.character_equipment().slot_idx());
      result.y = 0;
      break;
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory:
      // 虚拟仓库不落位, 坐标恒为 (0, 0)
      break;
    default:
      break;
  }
  return result;
}

template <typename ModeContainerT, typename DerivedT>
void user_item_container<ModeContainerT, DerivedT>::apply_position(
    PROJECT_NAMESPACE_ID::DItemGridPosition& position, const item_algorithm::ItemGridPosition& container_pos) const {
  switch (this->get_position_type()) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory: {
      auto* out = position.mutable_user_inventory();
      out->set_x(container_pos.x);
      out->set_y(container_pos.y);
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory: {
      auto* out = position.mutable_character_inventory();
      out->set_x(container_pos.x);
      out->set_y(container_pos.y);
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment:
      position.mutable_character_equipment()->set_slot_idx(
          static_cast<PROJECT_NAMESPACE_ID::EnEquipmentSlot>(container_pos.x));
      break;
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory:
      // proto 里是 bool: 只标记归属本容器, 不带坐标
      position.set_virtual_inventory(true);
      break;
    default:
      break;
  }
}

template <typename ModeContainerT, typename DerivedT>
void user_item_container<ModeContainerT, DerivedT>::on_item_data_changed(
    const item_algorithm::item_entry_ptr_t& entry, const item_algorithm::ItemOperationContext& context) {
  if (registered_) {
    owner_->get_user_item_container_manager().on_item_changed(this->get_container_guid(), entry, context);
  }
}

template <typename ModeContainerT, typename DerivedT>
void user_item_container<ModeContainerT, DerivedT>::on_item_count_changed(
    int32_t type_id, const item_algorithm::item_entry_ptr_t& /*entry*/, int64_t /*guid*/,
    const item_algorithm::ItemGridPosition& /*position*/, int64_t old_count, int64_t new_count,
    int64_t /*type_total_count*/, const item_algorithm::ItemOperationContext& /*context*/) {
  if (registered_) {
    owner_->get_user_item_container_manager().on_item_count_changed(type_id, new_count - old_count);
  }
}

// ============================================================
// 三种模式的业务容器
// ============================================================

user_item_finite_container::user_item_finite_container(user* owner, const std::string& log_category_prefix)
    : base_type(owner, log_category_prefix) {}
user_item_finite_container::~user_item_finite_container() = default;

void user_item_finite_container::init(int32_t row_size, int32_t column_size,
                                      PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                      int64_t container_guid) {
  if (this->is_initialized()) {
    FWLOGERROR("user_item_finite_container::init called twice, container_guid: {}", container_guid);
    return;
  }
  this->mode_container_type::init(row_size, column_size, position_type, container_guid);
  this->finish_init();
}

user_item_infinite_container::user_item_infinite_container(user* owner, const std::string& log_category_prefix)
    : base_type(owner, log_category_prefix) {}
user_item_infinite_container::~user_item_infinite_container() = default;

void user_item_infinite_container::init(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                        int64_t container_guid) {
  if (this->is_initialized()) {
    FWLOGERROR("user_item_infinite_container::init called twice, container_guid: {}", container_guid);
    return;
  }
  this->mode_container_type::init(position_type, container_guid);
  this->finish_init();
}

user_item_no_position_container::user_item_no_position_container(user* owner, const std::string& log_category_prefix)
    : base_type(owner, log_category_prefix) {}
user_item_no_position_container::~user_item_no_position_container() = default;

void user_item_no_position_container::init(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                           int64_t container_guid) {
  if (this->is_initialized()) {
    FWLOGERROR("user_item_no_position_container::init called twice, container_guid: {}", container_guid);
    return;
  }
  this->mode_container_type::init(position_type, container_guid);
  this->finish_init();
}

// ============================================================
// 显式实例化三种业务容器
//
// 模板定义放在本 .cpp 里 (不放进头文件, 避免把 SDK 容器实现细节与 owner 依赖扩散到每个
// 引用点), 因此必须在这里显式实例化, 否则其它 TU 拿不到成员函数的定义。
// ============================================================

template class user_item_container<item_algorithm::ItemFiniteGridContainer, user_item_finite_container>;
template class user_item_container<item_algorithm::ItemInfiniteGridContainer, user_item_infinite_container>;
template class user_item_container<item_algorithm::ItemNoPositionContainer, user_item_no_position_container>;
