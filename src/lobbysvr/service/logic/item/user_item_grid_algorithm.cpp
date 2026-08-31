// Copyright 2026 atframework

#include "logic/item/user_item_grid_algorithm.h"

#include <log/log_wrapper.h>

#include <data/user.h>
#include <logic/item/user_item_grid_manager.h>

#include <string>

user_item_grid_algorithm::user_item_grid_algorithm(user* owner, const std::string& log_category_prefix)
    : owner_(owner), log_category_prefix_(log_category_prefix) {
  item_algorithm::ItemLogHandler handler;
  handler.on_log = [this](const item_algorithm::ItemLogRecord& record) {
    ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level lv = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level::kDebug;
    switch (record.level) {
      case item_algorithm::ItemLogLevel::kDebug:
        lv = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level::kDebug;
        break;
      case item_algorithm::ItemLogLevel::kInfo:
        lv = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level::kInfo;
        break;
      case item_algorithm::ItemLogLevel::kWarning:
        lv = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level::kWarning;
        break;
      case item_algorithm::ItemLogLevel::kError:
        lv = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level::kError;
        break;
      default:
        break;
    }
    if (::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_wrapper::check_level(
            WDTLOGGETCAT(::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_wrapper::categorize_t::DEFAULT), lv))
      WDTLOGGETCAT(::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_wrapper::categorize_t::DEFAULT)
          ->format_log(::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_wrapper::caller_info_t(
                           lv, {}, record.file_name, static_cast<uint32_t>(record.line_number), ""),
                        "[{}][{}] {}", *owner_, record.category, record.message);
  };
  set_log_handler(handler);
}
user_item_grid_algorithm::~user_item_grid_algorithm() {}

void user_item_grid_algorithm::init(int32_t row_size, int32_t column_size,
                                    PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                    int64_t container_guid) {
  if (init_) {
    FWLOGERROR("user_item_grid_algorithm::init called twice for container_guid: {}", container_guid);
    return;
  }
  init_ = true;
  item_algorithm::ItemGridAlgorithmMode mode = item_algorithm::ItemGridAlgorithmMode::kFiniteGrid;
  switch (position_type) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory:
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory:
      mode = item_algorithm::ItemGridAlgorithmMode::kFiniteGrid;
      break;
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment:
      mode = item_algorithm::ItemGridAlgorithmMode::kInfiniteGrid;
      break;
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory:
      mode = item_algorithm::ItemGridAlgorithmMode::kNoPosition;
      break;
    default:
      FWLOGERROR("user_item_grid_algorithm::init unknown position_type: {} for container_guid: {}",
                 static_cast<int32_t>(position_type), container_guid);
      return;
  }
  item_algorithm::ItemGridAlgorithm::init(mode, row_size, column_size, position_type, container_guid);

  item_algorithm::ItemLogHandler handler = get_log_handler();
  if (log_category_prefix_.empty()) {
    log_category_prefix_ = handler.category;
  }
  handler.category = log_category_prefix_ + ":" + std::to_string(container_guid);
  set_log_handler(handler);
  // 注册映射
  owner_->get_user_item_grid_manager().register_item_grid_algorithm(
      atfw::util::memory::static_pointer_cast<user_item_grid_algorithm>(shared_from_this()));
  register_grid_ = true;
}

void user_item_grid_algorithm::on_item_data_changed(const item_algorithm::item_grid_entry_ptr_t& entry,
                                                    item_algorithm::ItemGridOperationReason clone_grid_reason) {
  if (register_grid_) {
    owner_->get_user_item_grid_manager().on_item_changed(get_container_guid(), entry, clone_grid_reason);
  }
}

void user_item_grid_algorithm::destroy() {
  if (register_grid_) {
    owner_->get_user_item_grid_manager().unregister_item_grid_algorithm(this);
    register_grid_ = false;
  }
}
