// Copyright 2025 atframework

#include "ItemAlgorithm/ItemGridAlgorithm.h"

#include "config/excel/config_manager.h"
#include "config/excel/item_type_config.h"

#include <algorithm>
#include <cassert>

#ifdef _MSC_VER
#  include <intrin.h>
#endif

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

ITEM_ALGORITHM_API ItemGridAlgorithm::ItemGridAlgorithm()
    : init_(false),
      mode_(ItemGridAlgorithmMode::kFiniteGrid),
      container_guid_(0),
      row_size_(0),
      column_size_(0),
      position_type_(PROJECT_NAMESPACE_ID::DItemGridPosition::POSITION_TYPE_NOT_SET),
      next_entry_id_(1),
      operate_id_(0) {}

ITEM_ALGORITHM_API ItemGridAlgorithm::~ItemGridAlgorithm() {}

ITEM_ALGORITHM_API void ItemGridAlgorithm::init(ItemGridAlgorithmMode mode, int32_t row_size, int32_t column_size,
                                                PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                                int64_t container_guid) {
  init_ = true;
  mode_ = mode;
  if (mode == ItemGridAlgorithmMode::kFiniteGrid) {
    row_size_ = row_size;
    column_size_ = column_size;
  }
  position_type_ = position_type;
  container_guid_ = container_guid;
  clear();

  if (is_occupy_flag()) {
    if (row_size_ <= 0) {
      ITEM_ALGORITHM_LOG_ERROR_FMT("init called with invalid row_size={}, reset to 1", row_size_);
      row_size_ = 1;
    }
    if (column_size_ <= 0) {
      ITEM_ALGORITHM_LOG_ERROR_FMT("init called with invalid column_size={}, reset to 1", column_size_);
      column_size_ = 1;
    }
    occupy_grid_flag_.resize(static_cast<size_t>(row_size_), static_cast<size_t>(column_size_));
  }

  ITEM_ALGORITHM_LOG_DEBUG_FMT("init grid rows={} cols={} position_type={} mode={}", row_size_, column_size_,
                               static_cast<int>(position_type_), static_cast<int>(mode_));
}

ITEM_ALGORITHM_API void ItemGridAlgorithm::foreach (
    std::function<bool(const PROJECT_NAMESPACE_ID::DItemInstance&)> fn) const {
  for (const auto& group_pair : item_groups_) {
    for (const auto& entry : group_pair.second) {
      if (entry) {
        if (!fn(entry->item_instance())) {
          return;
        }
      }
    }
  }
}

// ============================================================
// Clear / Get / 查询
// ============================================================

ITEM_ALGORITHM_API void ItemGridAlgorithm::clear() {
  item_groups_.clear();
  position_index_.clear();
  guid_index_.clear();
  entry_id_index_.clear();
  item_count_cache_.clear();

  occupy_grid_flag_.clear();

  ITEM_ALGORITHM_LOG_DEBUG("clear grid");
}

ITEM_ALGORITHM_API item_grid_entry_ptr_t
ItemGridAlgorithm::get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
  if (is_ignore_position()) {
    return nullptr;
  }
  ItemGridPosition grid_pos = extract_position(position);
  auto it = position_index_.find(grid_pos);
  if (it != position_index_.end()) {
    return it->second;
  }
  return nullptr;
}

ITEM_ALGORITHM_API item_grid_entry_ptr_t ItemGridAlgorithm::get_by_guid(int64_t guid) const {
  auto it = guid_index_.find(guid);
  if (it != guid_index_.end()) {
    return it->second;
  }
  return nullptr;
}

ITEM_ALGORITHM_API const ItemGridAlgorithm::item_group_type* ItemGridAlgorithm::get_group(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  if (it != item_groups_.end()) {
    return &it->second;
  }
  return nullptr;
}

ITEM_ALGORITHM_API int64_t ItemGridAlgorithm::get_item_count(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  if (it == item_groups_.end()) {
    return 0;
  }

  int64_t total = 0;
  for (const auto& entry : it->second) {
    if (entry) {
      total += entry->item_instance().item_basic().count();
    }
  }
  return total;
}

ITEM_ALGORITHM_API int64_t ItemGridAlgorithm::get_cached_item_count(int32_t type_id) const {
  auto it = item_count_cache_.find(type_id);
  return (it != item_count_cache_.end()) ? it->second : 0;
}

// ============================================================
// 日志接口
// ============================================================

ITEM_ALGORITHM_API void ItemGridAlgorithm::set_log_handler(const ItemLogHandler& handler) { log_handler_ = handler; }

ITEM_ALGORITHM_API const ItemLogHandler& ItemGridAlgorithm::get_log_handler() const { return log_handler_; }

ITEM_ALGORITHM_API void ItemGridAlgorithm::log(ItemLogLevel level, const char* file_name, int line_number,
                                               const std::string& message) const {
  if (!log_handler_.on_log) {
    return;
  }

  ItemLogRecord record;
  record.level = level;
  record.file_name = file_name;
  record.line_number = line_number;
  record.category = log_handler_.category;
  record.message = message;
  log_handler_.on_log(record);
}

ITEM_ALGORITHM_API bool ItemGridAlgorithm::is_empty() const { return item_groups_.empty(); }

ITEM_ALGORITHM_API const ItemGridAlgorithm::item_group_map_type& ItemGridAlgorithm::get_all_groups() const {
  return item_groups_;
}

ITEM_ALGORITHM_API const ItemGridOccupyFlag& ItemGridAlgorithm::get_occupy_grid_flag() const {
  return occupy_grid_flag_;
}

ITEM_ALGORITHM_API int32_t ItemGridAlgorithm::get_row_size() const { return row_size_; }

ITEM_ALGORITHM_API int32_t ItemGridAlgorithm::get_column_size() const { return column_size_; }

ITEM_ALGORITHM_API int64_t ItemGridAlgorithm::get_container_guid() const { return container_guid_; }

// ============================================================
// 位置转换辅助
// ============================================================

ITEM_ALGORITHM_API ItemGridPosition
ItemGridAlgorithm::extract_position(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
  ItemGridPosition result;
  switch (position_type_) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory: {
      result.x = position.user_inventory().x();
      result.y = position.user_inventory().y();
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory: {
      result.x = position.character_inventory().x();
      result.y = position.character_inventory().y();
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment: {
      result.x = static_cast<int32_t>(position.character_equipment().slot_idx());
      result.y = 0;
      break;
    }
    default:
      break;
  }
  return result;
}

// ============================================================
// Replace check 辅助 — 默认实现
// ============================================================

ITEM_ALGORITHM_API item_grid_algorithm_ptr_t ItemGridAlgorithm::create_empty_clone() const {
  auto grid = atfw::util::memory::make_strong_rc<ItemGridAlgorithm>();
  copy_empty_config_to(*grid);
  return grid;
}

ITEM_ALGORITHM_API void ItemGridAlgorithm::copy_empty_config_to(ItemGridAlgorithm& out) const {
  out.init(mode_, row_size_, column_size_, position_type_, container_guid_);
}

// ============================================================
// 虚函数钩子 — 默认实现
// ============================================================

ITEM_ALGORITHM_API const PROJECT_NAMESPACE_ID::DItemPositionCfg* ItemGridAlgorithm::get_item_position_cfg(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  auto item_row = config_group->ExcelItemType.get_by_type_id(basic.type_id());
  if (!item_row) {
    return nullptr;
  }
  auto ue_item_row = config_group->UESourceInventory.get_by_type_id(item_row->ue_source_type_id());
  if (!ue_item_row) {
    return nullptr;
  }
  return &ue_item_row->position_cfg();
}

ITEM_ALGORITHM_API int32_t ItemGridAlgorithm::on_check_add(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const PROJECT_NAMESPACE_ID::DItemInstance& /*request*/) const {
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API int32_t ItemGridAlgorithm::on_check_sub(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const PROJECT_NAMESPACE_ID::DItemBasic& /*request*/) const {
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API int32_t ItemGridAlgorithm::on_check_item_count_limit(int32_t /*type_id*/, int64_t /*current_count*/,
                                                                        int64_t /*add_count*/) const {
  // 默认不限制, 子类按需覆盖
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API bool ItemGridAlgorithm::on_find_position_for_infinite(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const PROJECT_NAMESPACE_ID::DItemBasic& /*basic*/, PROJECT_NAMESPACE_ID::DItemGridPosition& /*out_pos*/) const {
  // 默认无法确定位置, 子类按需覆盖 (如装备槽按 type_id 映射 slot_idx)
  return false;
}

ITEM_ALGORITHM_API void ItemGridAlgorithm::on_item_count_changed(
    int32_t /*type_id*/, const item_grid_entry_ptr_t& /*entry*/, int64_t /*guid*/, const ItemGridPosition& /*position*/,
    int64_t /*old_count*/, int64_t /*new_count*/, int64_t /*type_total_count*/, ItemGridOperationReason /*reason*/) {
  // 默认空实现, 子类按需覆盖
}

ITEM_ALGORITHM_API void ItemGridAlgorithm::on_item_data_changed(const item_grid_entry_ptr_t& /*entry*/,
                                                                ItemGridOperationReason /*reason*/) {
  // 默认空实现, 子类按需覆盖
}

ITEM_ALGORITHM_API bool ItemGridAlgorithm::check_item_position(
    const PROJECT_NAMESPACE_ID::DItemPosition& /*position*/) const {
  return true;  // 默认不检查, 子类按需覆盖
}

ITEM_ALGORITHM_API int32_t ItemGridAlgorithm::on_item_not_enough(int32_t /*type_id*/) const {
  return PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
}

ITEM_ALGORITHM_API item_grid_entry_ptr_t
ItemGridAlgorithm::find_entry(const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  int64_t guid = basic.guid();

  if (guid != 0) {
    return get_by_guid(guid);
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
  if (item_type_config == nullptr) {
    return nullptr;
  }

  if (item_type_config->need_occupy_the_grid) {
    // 占格道具: 按位置查找
    ItemGridPosition pos = extract_position(basic.position().grid_position());
    auto it = position_index_.find(pos);
    if (it != position_index_.end()) {
      return it->second;
    }
  } else {
    // 不占格道具: 按类型查找
    int32_t type_id = basic.type_id();
    auto it = item_groups_.find(type_id);
    if (it != item_groups_.end() && !it->second.empty()) {
      return *it->second.begin();
    }
  }
  return nullptr;
}

ITEM_ALGORITHM_API item_grid_entry_ptr_t ItemGridAlgorithm::find_entry_by_id(uint64_t entry_id) const {
  auto it = entry_id_index_.find(entry_id);
  if (it != entry_id_index_.end()) {
    return it->second.lock();
  }
  return nullptr;
}

// ============================================================
// 内部实现
// ============================================================

bool ItemGridAlgorithm::is_occupy_flag() const { return mode_ == ItemGridAlgorithmMode::kFiniteGrid; }
bool ItemGridAlgorithm::is_ignore_position() const { return mode_ == ItemGridAlgorithmMode::kNoPosition; }

void ItemGridAlgorithm::apply_position(PROJECT_NAMESPACE_ID::DItemGridPosition& position,
                                       const ItemGridPosition& grid_pos) const {
  switch (position_type_) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory: {
      position.mutable_user_inventory()->set_x(grid_pos.x);
      position.mutable_user_inventory()->set_y(grid_pos.y);
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory: {
      position.mutable_character_inventory()->set_x(grid_pos.x);
      position.mutable_character_inventory()->set_y(grid_pos.y);
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment: {
      position.mutable_character_equipment()->set_slot_idx(
          static_cast<PROJECT_NAMESPACE_ID::EnEquipmentSlot>(grid_pos.x));
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory: {
      position.set_virtual_inventory(false);
      break;
    }
    default:
      break;
  }
}

bool ItemGridAlgorithm::is_item_valid(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  if (basic.type_id() == 0 || basic.count() <= 0) {
    return false;
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
  if (item_type_config == nullptr) {
    return false;
  }

  if (item_type_config->need_occupy_the_grid) {
    // 无位置模式不能占格子
    if (is_ignore_position()) {
      return false;
    }
    auto position_cfg = get_item_position_cfg(config_group, basic);
    if (position_cfg == nullptr) {
      return false;
    }
  } else {
    // 不占格道具 必须放再无位置模式里
    if (!is_ignore_position()) {
      return false;
    }
  }

  if (item_type_config->need_guid) {
    // 没有位置不允许有guid
    if (is_ignore_position()) {
      return false;
    }
    if (basic.guid() == 0) {
      return false;
    }
  } else {
    if (basic.guid() != 0) {
      return false;
    }
  }
  return true;
}

bool ItemGridAlgorithm::is_item_in_range(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const {
  if (!is_occupy_flag()) {
    return true;
  }
  return x >= 0 && y >= 0 && x + item_col_size <= column_size_ && y + item_row_size <= row_size_;
}

bool ItemGridAlgorithm::check_collision(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const {
  if (!is_occupy_flag()) {
    if (is_ignore_position()) {
      return false;
    }
    ItemGridPosition pos{x, y};
    auto it = position_index_.find(pos);
    return it != position_index_.end();
  }

  for (int32_t dr = 0; dr < item_row_size; ++dr) {
    for (int32_t dc = 0; dc < item_col_size; ++dc) {
      int32_t r = y + dr;
      int32_t c = x + dc;
      if (r < 0 || r >= row_size_ || c < 0 || c >= column_size_) {
        return true;
      }
      if (occupy_grid_flag_.is_occupied(c, r)) {
        return true;
      }
    }
  }
  return false;
}

void ItemGridAlgorithm::set_grid_flag(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size,
                                      bool occupied) {
  if (!is_occupy_flag()) {
    return;
  }

  for (int32_t dr = 0; dr < item_row_size; ++dr) {
    for (int32_t dc = 0; dc < item_col_size; ++dc) {
      int32_t r = y + dr;
      int32_t c = x + dc;
      if (r >= 0 && r < row_size_ && c >= 0 && c < column_size_) {
        occupy_grid_flag_.set(c, r, occupied);
      }
    }
  }
}

void ItemGridAlgorithm::remove_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                           const item_grid_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance().item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance().item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    position_index_.erase(pos);
  }

  if (is_occupy_flag() && item_type_config->need_occupy_the_grid) {
    int32_t item_row = position_cfg.row_size();
    int32_t item_col = position_cfg.column_size();
    set_grid_flag(pos.x, pos.y, item_row, item_col, false);
  }

  int64_t guid = entry->item_instance().item_basic().guid();
  if (guid != 0) {
    guid_index_.erase(guid);
  }
}

void ItemGridAlgorithm::remove_entry_from_group(const item_grid_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  int32_t type_id = entry->item_instance().item_basic().type_id();
  auto group_it = item_groups_.find(type_id);
  if (group_it != item_groups_.end()) {
    group_it->second.erase(entry);
    if (group_it->second.empty()) {
      item_groups_.erase(group_it);
    }
  }
}

void ItemGridAlgorithm::add_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                        const item_grid_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance().item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance().item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    position_index_[pos] = entry;
  }

  if (is_occupy_flag()) {
    int32_t item_row = position_cfg.row_size();
    int32_t item_col = position_cfg.column_size();
    set_grid_flag(pos.x, pos.y, item_row, item_col, true);
  }

  int64_t guid = entry->item_instance().item_basic().guid();
  if (guid != 0) {
    guid_index_[guid] = entry;
  }
}

void ItemGridAlgorithm::add_entry_id_index(const item_grid_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }
  entry_id_index_[entry->entry_id()] = entry;
}

void ItemGridAlgorithm::remove_entry_id_index(uint64_t entry_id) { entry_id_index_.erase(entry_id); }

item_grid_entry_ptr_t ItemGridAlgorithm::make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance) {
  auto entry =
      atfw::util::memory::make_strong_rc<ItemGridEntry>(shared_from_this(), std::move(instance), next_entry_id_++);
  add_entry_id_index(entry);
  return entry;
}

item_grid_entry_ptr_t ItemGridAlgorithm::make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance, uint64_t entry_id) {
  auto entry = atfw::util::memory::make_strong_rc<ItemGridEntry>(shared_from_this(), std::move(instance), entry_id);
  add_entry_id_index(entry);
  return entry;
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
