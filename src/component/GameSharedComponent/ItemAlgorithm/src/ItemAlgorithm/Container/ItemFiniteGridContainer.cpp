// Copyright 2026 atframework

#include "ItemAlgorithm/Container/ItemFiniteGridContainer.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "config/excel/config_manager.h"
#include "config/excel/item_type_config.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

namespace {

/// @brief 有限格子的批次预留内容: 位图副本
///
/// check_add / check_move 需要在批次内判断"前面请求已经占掉的格子",
/// 因此复制一份位图, 在该副本上做检查与占用, 不污染容器自身的位图。
class ItemFiniteGridPlacementReservation : public ItemPlacementReservation {
 public:
  ItemFiniteGridPlacementReservation() = default;
  ~ItemFiniteGridPlacementReservation() override = default;

  ItemGridOccupancy occupancy;
};

}  // namespace

ITEM_ALGORITHM_API ItemFiniteGridContainer::ItemFiniteGridContainer() : row_size_(0), column_size_(0) {}

ITEM_ALGORITHM_API ItemFiniteGridContainer::~ItemFiniteGridContainer() {}

// ============================================================
// 批次内位置预留 (本模式专用)
// ============================================================

ITEM_ALGORITHM_API ItemPlacementReservation::ItemPlacementReservation() {}

ITEM_ALGORITHM_API ItemPlacementReservation::~ItemPlacementReservation() {}

ITEM_ALGORITHM_API ItemPlacementReservation::ItemPlacementReservation(ItemPlacementReservation&&) noexcept {}

ITEM_ALGORITHM_API ItemPlacementReservation& ItemPlacementReservation::operator=(ItemPlacementReservation&&) noexcept {
  return *this;
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::init(
    int32_t row_size, int32_t column_size, PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
    int64_t container_guid) {
  init_container(container_guid);
  position_type_ = position_type;

  row_size_ = row_size;
  column_size_ = column_size;
  if (row_size_ <= 0) {
    FWINSTLOGERROR(logger(), "init called with invalid row_size={}, reset to 1", row_size_);
    row_size_ = 1;
  }
  if (column_size_ <= 0) {
    FWINSTLOGERROR(logger(), "init called with invalid column_size={}, reset to 1", column_size_);
    column_size_ = 1;
  }
  occupy_grid_flag_.resize(static_cast<size_t>(row_size_), static_cast<size_t>(column_size_));

  FWINSTLOGDEBUG(logger(), "init finite grid rows={} cols={} position_type={} container_guid={}", row_size_,
                 column_size_, static_cast<int>(position_type_), container_guid);
}

ITEM_ALGORITHM_API int32_t ItemFiniteGridContainer::get_row_size() const { return row_size_; }

ITEM_ALGORITHM_API int32_t ItemFiniteGridContainer::get_column_size() const { return column_size_; }

ITEM_ALGORITHM_API const ItemGridOccupancy& ItemFiniteGridContainer::get_occupy_grid_flag() const {
  return occupy_grid_flag_;
}

ITEM_ALGORITHM_API item_placement_reservation_ptr_t ItemFiniteGridContainer::create_placement_reservation() const {
  auto reservation = std::unique_ptr<ItemFiniteGridPlacementReservation>(new ItemFiniteGridPlacementReservation());
  reservation->occupancy = occupy_grid_flag_;
  return reservation;
}

ITEM_ALGORITHM_API bool ItemFiniteGridContainer::is_occupancy_blocked(int32_t x, int32_t y, int32_t item_row_size,
                                                                      int32_t item_col_size,
                                                                      ItemPlacementReservation& reservation) const {
  auto* finite_reservation = dynamic_cast<ItemFiniteGridPlacementReservation*>(&reservation);
  if (nullptr == finite_reservation) {
    return false;
  }

  ItemGridOccupancy& occupancy = finite_reservation->occupancy;
  for (int32_t dr = 0; dr < item_row_size; ++dr) {
    for (int32_t dc = 0; dc < item_col_size; ++dc) {
      if (occupancy.is_occupied(x + dc, y + dr)) {
        return true;
      }
      occupancy.set(x + dc, y + dr, true);
    }
  }
  return false;
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::release_reserved_position(
    int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size, ItemPlacementReservation& reservation) const {
  auto* finite_reservation = dynamic_cast<ItemFiniteGridPlacementReservation*>(&reservation);
  if (nullptr == finite_reservation) {
    return;
  }

  ItemGridOccupancy& occupancy = finite_reservation->occupancy;
  for (int32_t dr = 0; dr < item_row_size; ++dr) {
    for (int32_t dc = 0; dc < item_col_size; ++dc) {
      occupancy.set(x + dc, y + dr, false);
    }
  }
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::on_update_grid_occupancy(
    const ItemGridPosition& position, const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg, bool occupied) {
  int32_t item_row = position_cfg.row_size();
  int32_t item_col = position_cfg.column_size();
  for (int32_t dr = 0; dr < item_row; ++dr) {
    for (int32_t dc = 0; dc < item_col; ++dc) {
      occupy_grid_flag_.set(position.x + dc, position.y + dr, occupied);
    }
  }
}

// ============================================================
// 位置与身份索引
// ============================================================

ITEM_ALGORITHM_API void ItemFiniteGridContainer::on_clear() {
  position_index_.clear();
  guid_index_.clear();
  occupy_grid_flag_.clear();
}

ITEM_ALGORITHM_API item_entry_ptr_t
ItemFiniteGridContainer::find_entry_at_position(const ItemGridPosition& position) const {
  auto it = position_index_.find(position);
  return (it != position_index_.end()) ? it->second : nullptr;
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::attach_entry_position(const ItemGridPosition& position,
                                                                       const item_entry_ptr_t& entry) {
  position_index_[position] = entry;
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::detach_entry_position(const ItemGridPosition& position) {
  position_index_.erase(position);
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemFiniteGridContainer::find_entry_by_guid(int64_t guid) const {
  auto it = guid_index_.find(guid);
  return (it != guid_index_.end()) ? it->second : nullptr;
}

ITEM_ALGORITHM_API bool ItemFiniteGridContainer::has_entry_guid(int64_t guid) const {
  return guid_index_.find(guid) != guid_index_.end();
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::attach_entry_guid(int64_t guid, const item_entry_ptr_t& entry) {
  guid_index_[guid] = entry;
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::detach_entry_guid(int64_t guid) { guid_index_.erase(guid); }

// ============================================================
// 索引维护 (本模式的操作流程调用)
// ============================================================

ITEM_ALGORITHM_API void ItemFiniteGridContainer::add_entry_index(
    const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg, const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance().item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance().item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    attach_entry_position(pos, entry);
    on_update_grid_occupancy(pos, position_cfg, true);
  }

  int64_t guid = entry->item_instance().item_basic().guid();
  if (guid != 0) {
    attach_entry_guid(guid, entry);
  }
}

ITEM_ALGORITHM_API void ItemFiniteGridContainer::remove_entry_index(
    const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg, const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance().item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance().item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    detach_entry_position(pos);
    on_update_grid_occupancy(pos, position_cfg, false);
  }

  int64_t guid = entry->item_instance().item_basic().guid();
  if (guid != 0) {
    detach_entry_guid(guid);
  }
}

// ============================================================
// 模式相关公开接口 (本模式实现)
// ============================================================

ITEM_ALGORITHM_API uint64_t
ItemFiniteGridContainer::make_entry_sort_key(const PROJECT_NAMESPACE_ID::DItemInstance& instance) const {
  // 位置知识留在容器这一侧: 条目只保存这个不透明键。
  // 坐标经接入层实现的 extract_position 从 proto 位置字段取出, 库内不解析具体字段
  ItemGridPosition pos = extract_position(instance.item_basic().position().grid_position());
  return (static_cast<uint64_t>(pos.y) << 32) + (static_cast<uint64_t>(pos.x) & 0xFFFFFFFF);
}

// ============================================================
// 条目校验 (本模式实现)
// ============================================================

ITEM_ALGORITHM_API bool ItemFiniteGridContainer::is_item_valid(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  if (basic.type_id() == 0 || basic.count() <= 0) {
    return false;
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
  if (item_type_config == nullptr) {
    return false;
  }

  // 有限格子只放占格道具, 且必须有位置配置
  if (!item_type_config->need_occupy_the_grid) {
    return false;
  }
  if (get_item_position_cfg(config_group, basic) == nullptr) {
    return false;
  }

  if (item_type_config->need_guid) {
    if (basic.guid() == 0) {
      return false;
    }
  } else if (basic.guid() != 0) {
    return false;
  }
  return true;
}

ITEM_ALGORITHM_API item_entry_ptr_t
ItemFiniteGridContainer::get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
  return find_entry_at_position(extract_position(position));
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemFiniteGridContainer::get_by_guid(int64_t guid) const {
  return find_entry_by_guid(guid);
}

ITEM_ALGORITHM_API bool ItemFiniteGridContainer::is_item_in_range(int32_t x, int32_t y, int32_t item_row_size,
                                                                  int32_t item_col_size) const {
  return x >= 0 && y >= 0 && x + item_col_size <= column_size_ && y + item_row_size <= row_size_;
}

ITEM_ALGORITHM_API bool ItemFiniteGridContainer::check_collision(int32_t x, int32_t y, int32_t item_row_size,
                                                                 int32_t item_col_size) const {
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

ITEM_ALGORITHM_API void ItemFiniteGridContainer::get_entries_in_area(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group, int32_t x, int32_t y,
    int32_t row_size, int32_t column_size, std::vector<item_entry_ptr_t>& out_entries) const {
  out_entries.clear();
  if (row_size <= 0 || column_size <= 0) {
    return;
  }

  // 占格道具的尺寸来自配置, 位置索引只记锚点, 因此只能遍历条目逐个做矩形相交判定
  for (const auto& group_pair : get_all_groups()) {
    for (const auto& entry : group_pair.second.entries) {
      if (!entry) {
        continue;
      }

      const auto& basic = entry->item_instance().item_basic();
      auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
      if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
        // 不占格道具没有区域
        continue;
      }

      auto position_cfg = get_item_position_cfg(config_group, basic);
      if (position_cfg == nullptr) {
        continue;
      }

      ItemGridPosition anchor = extract_position(basic.position().grid_position());
      if (anchor.x + position_cfg->column_size() <= x || x + column_size <= anchor.x) {
        continue;
      }
      if (anchor.y + position_cfg->row_size() <= y || y + row_size <= anchor.y) {
        continue;
      }

      out_entries.push_back(entry);
    }
  }

  // 分组表是无序遍历, 结果按 (行, 列, entry_id) 排序, 保证调用方拿到稳定顺序
  std::sort(out_entries.begin(), out_entries.end(), [this](const item_entry_ptr_t& lhs, const item_entry_ptr_t& rhs) {
    ItemGridPosition lhs_pos = extract_position(lhs->item_instance().item_basic().position().grid_position());
    ItemGridPosition rhs_pos = extract_position(rhs->item_instance().item_basic().position().grid_position());
    if (lhs_pos.y != rhs_pos.y) {
      return lhs_pos.y < rhs_pos.y;
    }
    if (lhs_pos.x != rhs_pos.x) {
      return lhs_pos.x < rhs_pos.x;
    }
    return lhs->entry_id() < rhs->entry_id();
  });
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
