// Copyright 2025 atframework

#include "ItemAlgorithm/ItemGridAlgorithm.h"

#include "config/excel/config_manager.h"
#include "config/excel/item_type_config.h"

#include <algorithm>
#include <cassert>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// ItemGridAlgorithm
// ============================================================

ItemGridAlgorithm::ItemGridAlgorithm() = default;
ItemGridAlgorithm::~ItemGridAlgorithm() = default;

void ItemGridAlgorithm::init(int32_t row_size, int32_t column_size,
                             PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type) {
  row_size_ = row_size;
  column_size_ = column_size;
  position_type_ = position_type;

  switch (position_type) {
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory:
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory: {
      is_care_item_size_ = true;
      break;
    }
    case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment: {
      is_care_item_size_ = false;
      break;
    }
    default:
      break;
  }
  clear();

  if (is_care_item_size()) {
    if (row_size_ <= 0) {
      row_size_ = 1;
    }
    if (column_size_ <= 0) {
      column_size_ = 1;
    }
    occupy_grid_flag_.resize(static_cast<size_t>(row_size_));
    for (auto& row : occupy_grid_flag_) {
      row.resize(static_cast<size_t>(column_size_), false);
    }
  }
}

ItemGridOperationResult ItemGridAlgorithm::add(const ItemGridAddCheckedRequest& checked_request) {
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request.result;
  }

  ItemGridOperationResult result;

  const auto& requests = checked_request.requests;

  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& req = requests[i];
    const auto& item_basic = req.item_instance->item_basic();
    int32_t type_id = item_basic.type_id();
    int64_t add_count = item_basic.count();
    int64_t guid = item_basic.guid();

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);

    // 不占格道具: 只放入 item_groups_ + guid_index_
    if (!item_type_config->need_occupy_the_grid) {
      if (!item_type_config->need_guid) {
        // 不需要GUID: 合并到已有entry (entry数量始终为1, 无堆叠上限)
        auto group_it = item_groups_.find(type_id);
        if (group_it != item_groups_.end() && !group_it->second.empty()) {
          auto& existing = group_it->second.front();
          int64_t old_count = existing->item_instance.item_basic().count();
          existing->item_instance.mutable_item_basic()->set_count(old_count + add_count);
          item_count_cache_[type_id] += add_count;
          on_item_count_changed(type_id, existing, 0, ItemGridPosition{}, old_count, old_count + add_count,
                                item_count_cache_[type_id], ItemGridOperationReason::kAdd);
          on_item_data_changed(existing, ItemGridOperationReason::kAdd);
          continue;
        }
      }

      // 需要GUID 或 首次添加: 新建entry
      PROJECT_NAMESPACE_ID::DItemInstance new_instance = *req.item_instance;
      item_grid_entry_ptr_t entry = make_entry(std::move(new_instance));
      item_groups_[type_id].push_back(entry);
      if (guid != 0) {
        guid_index_[guid] = entry;
      }
      item_count_cache_[type_id] += add_count;
      on_item_count_changed(type_id, entry, guid, ItemGridPosition{}, 0, add_count, item_count_cache_[type_id],
                            ItemGridOperationReason::kAdd);
      on_item_data_changed(entry, ItemGridOperationReason::kAdd);
      continue;
    }

    // 占格道具
    ItemGridPosition target_pos = extract_position(item_basic.position().grid_position());

    // 尝试堆叠到已有条目 (无GUID才可堆叠)
    auto pos_it = position_index_.find(target_pos);
    if (pos_it != position_index_.end()) {
      auto& existing = pos_it->second;
      int64_t old_count = existing->item_instance.item_basic().count();
      existing->item_instance.mutable_item_basic()->set_count(old_count + add_count);
      item_count_cache_[type_id] += add_count;
      on_item_count_changed(type_id, existing, existing->item_instance.item_basic().guid(), target_pos, old_count,
                            old_count + add_count, item_count_cache_[type_id], ItemGridOperationReason::kAdd);
      on_item_data_changed(existing, ItemGridOperationReason::kAdd);
      continue;
    }

    // 新建条目 — 使用 check_add 已填充的 position_cfg
    PROJECT_NAMESPACE_ID::DItemInstance new_instance = *req.item_instance;
    apply_position(*new_instance.mutable_item_basic()->mutable_position()->mutable_grid_position(), target_pos);

    item_grid_entry_ptr_t entry = make_entry(std::move(new_instance));
    item_groups_[type_id].push_back(entry);
    add_entry_index(*get_item_position_cfg(checked_request.config_group, req.item_instance->item_basic()), entry);
    item_count_cache_[type_id] += add_count;
    on_item_count_changed(type_id, entry, entry->item_instance.item_basic().guid(), target_pos, 0, add_count,
                          item_count_cache_[type_id], ItemGridOperationReason::kAdd);
    on_item_data_changed(entry, ItemGridOperationReason::kAdd);
  }

  return result;
}

ItemGridAddCheckedRequest ItemGridAlgorithm::check_add(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<ItemGridAddRequest>& requests) const {
  ItemGridAddCheckedRequest checked_request{config_group, requests};
  auto& result = checked_request.result;

  std::vector<std::vector<bool>> tmp_grid_flag;
  if (is_care_item_size()) {
    tmp_grid_flag = occupy_grid_flag_;
  }

  std::unordered_set<int64_t> pending_guids;
  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_existing_extra;

  struct PendingNewSlot {
    int32_t type_id = 0;
    int64_t accumulated_count = 0;
  };
  std::unordered_map<ItemGridPosition, PendingNewSlot, ItemGridPositionHash, ItemGridPositionEqualTo> pending_new_slots;
  std::unordered_map<int32_t, int64_t> pending_type_add_count;

  for (size_t i = 0; i < checked_request.requests.size(); ++i) {
    const auto& req = checked_request.requests[i];
    if (req.item_instance == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    const auto& item_basic = req.item_instance->item_basic();
    int64_t guid = item_basic.guid();
    int32_t type_id = item_basic.type_id();
    int64_t add_count = item_basic.count();

    if (!is_item_valid(config_group, item_basic)) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    if (guid != 0) {
      if (guid_index_.count(guid) > 0 || pending_guids.count(guid) > 0) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }
    }

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    {
      int64_t current_total = get_cached_item_count(type_id) + pending_type_add_count[type_id];
      int32_t limit_ret = on_check_item_count_limit(type_id, current_total, add_count);
      if (limit_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
        result.error_code = limit_ret;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }
      pending_type_add_count[type_id] += add_count;
    }

    if (item_type_config->need_occupy_the_grid) {
      auto position_cfg = get_item_position_cfg(config_group, item_basic);
      if (position_cfg == nullptr) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }

      int64_t accumulation_limit = position_cfg->accumulation_limit();
      if (accumulation_limit <= 0) {
        accumulation_limit = 1;
      }

      if (add_count > accumulation_limit) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }

      ItemGridPosition target_pos = extract_position(item_basic.position().grid_position());
      auto pos_it = position_index_.find(target_pos);
      if (pos_it != position_index_.end()) {
        const auto& existing_basic = pos_it->second->item_instance.item_basic();
        if (guid != 0 || existing_basic.guid() != 0 || existing_basic.type_id() != type_id) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }

        int64_t total = existing_basic.count() + pending_existing_extra[target_pos] + add_count;
        if (total > accumulation_limit) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }
        pending_existing_extra[target_pos] += add_count;
      } else {
        auto pending_it = pending_new_slots.find(target_pos);
        if (pending_it != pending_new_slots.end()) {
          if (guid != 0 || pending_it->second.type_id != type_id) {
            result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
            result.failed_index = static_cast<int32_t>(i);
            return checked_request;
          }
          int64_t total = pending_it->second.accumulated_count + add_count;
          if (total > accumulation_limit) {
            result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
            result.failed_index = static_cast<int32_t>(i);
            return checked_request;
          }
          pending_it->second.accumulated_count = total;
        } else {
          if (is_care_item_size()) {
            int32_t item_row = position_cfg->row_size();
            int32_t item_col = position_cfg->column_size();
            if (item_row <= 0 || item_col <= 0) {
              result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
              result.failed_index = static_cast<int32_t>(i);
              return checked_request;
            }

            if (!is_item_in_range(target_pos.x, target_pos.y, item_row, item_col)) {
              result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OUT_OF_RANGE;
              result.failed_index = static_cast<int32_t>(i);
              return checked_request;
            }

            for (int32_t dr = 0; dr < item_row; ++dr) {
              for (int32_t dc = 0; dc < item_col; ++dc) {
                if (tmp_grid_flag[static_cast<size_t>(target_pos.y + dr)][static_cast<size_t>(target_pos.x + dc)]) {
                  result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
                  result.failed_index = static_cast<int32_t>(i);
                  return checked_request;
                }
                tmp_grid_flag[static_cast<size_t>(target_pos.y + dr)][static_cast<size_t>(target_pos.x + dc)] = true;
              }
            }
          }

          pending_new_slots[target_pos] = PendingNewSlot{type_id, add_count};
        }
      }
    }

    int32_t extra_ret = on_check_add(config_group, req);
    if (extra_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = extra_ret;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    if (guid != 0) {
      pending_guids.insert(guid);
    }
  }

  return checked_request;
}

ItemGridOperationResult ItemGridAlgorithm::sub(const ItemGridSubCheckedRequest& checked_request) {
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request.result;
  }

  ItemGridOperationResult result;

  const auto& requests = checked_request.requests;

  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& req = requests[i];
    item_grid_entry_ptr_t entry = find_entry(*req.item_basic);

    int64_t sub_count = req.item_basic->count();
    int64_t current_count = entry->item_instance.item_basic().count();
    int32_t type_id = entry->item_instance.item_basic().type_id();
    int64_t guid = entry->item_instance.item_basic().guid();
    ItemGridPosition entry_pos = extract_position(entry->item_instance.item_basic().position().grid_position());

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);

    if (current_count <= sub_count) {
      // 完全移除
      if (item_type_config->need_occupy_the_grid) {
        // 占格: 使用 check_sub 已填充的 position_cfg
        remove_entry_index(*get_item_position_cfg(checked_request.config_group, *req.item_basic), entry);
      } else {
        // 不占格道具: 仅移除 guid_index
        if (guid != 0) {
          guid_index_.erase(guid);
        }
      }
      remove_entry_from_group(entry);
      entry->item_instance.mutable_item_basic()->set_count(0);
      item_count_cache_[type_id] -= current_count;
      if (item_count_cache_[type_id] <= 0) {
        item_count_cache_.erase(type_id);
      }
      on_item_count_changed(type_id, entry, guid, entry_pos, current_count, 0, get_cached_item_count(type_id),
                            ItemGridOperationReason::kSub);
      on_item_data_changed(entry, ItemGridOperationReason::kSub);
    } else {
      // 部分扣减
      entry->item_instance.mutable_item_basic()->set_count(current_count - sub_count);
      item_count_cache_[type_id] -= sub_count;
      on_item_count_changed(type_id, entry, guid, entry_pos, current_count, current_count - sub_count,
                            item_count_cache_[type_id], ItemGridOperationReason::kSub);
      on_item_data_changed(entry, ItemGridOperationReason::kSub);
    }
  }

  return result;
}

ItemGridSubCheckedRequest ItemGridAlgorithm::check_sub(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<ItemGridSubRequest>& requests) const {
  ItemGridSubCheckedRequest checked_request{config_group, requests};
  auto& result = checked_request.result;

  std::unordered_set<int64_t> guid_sub;
  std::unordered_map<int32_t, int64_t> type_sub_count;
  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> position_sub_count;

  for (size_t i = 0; i < checked_request.requests.size(); ++i) {
    const auto& req = checked_request.requests[i];
    if (req.item_basic == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    int64_t guid = req.item_basic->guid();
    int32_t type_id = req.item_basic->type_id();
    int64_t sub_count = req.item_basic->count();

    if (!is_item_valid(config_group, *req.item_basic)) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    if (sub_count <= 0) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }

    // 占格道具: 查表并填充 position_cfg
    if (item_type_config->need_occupy_the_grid) {
      auto position_cfg = get_item_position_cfg(config_group, *req.item_basic);
      if (position_cfg == nullptr) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }
    }

    if (guid != 0) {
      // 带GUID 跳过位置直接索引
      if (sub_count != 1) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }

      auto guid_it = guid_index_.find(guid);
      if (guid_it == guid_index_.end()) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }

      // 检查数量与类型
      if (guid_it->second->item_instance.item_basic().type_id() != type_id ||
          guid_it->second->item_instance.item_basic().count() < sub_count) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }

      if (guid_sub.count(guid) > 0) {
        result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID;
        result.failed_index = static_cast<int32_t>(i);
        return checked_request;
      }
      guid_sub.insert(guid);
    } else {
      if (item_type_config->need_occupy_the_grid) {
        // 占格道具: 按位置预扣减, 跟踪同一位置多次扣减
        ItemGridPosition target_pos = extract_position(req.item_basic->position().grid_position());
        auto pos_it = position_index_.find(target_pos);
        if (pos_it == position_index_.end()) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }

        const auto& entry_basic = pos_it->second->item_instance.item_basic();
        if (entry_basic.type_id() != type_id) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }

        int64_t current = entry_basic.count();
        int64_t already_sub = position_sub_count[target_pos];
        if (current - already_sub < sub_count) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }
        position_sub_count[target_pos] = already_sub + sub_count;
      } else {
        // 不占格道具: 按类型总数扣减
        auto group_it = item_groups_.find(type_id);
        if (group_it == item_groups_.end()) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }

        int64_t total_count = 0;
        for (const auto& entry : group_it->second) {
          if (entry) {
            total_count += entry->item_instance.item_basic().count();
          }
        }

        int64_t already_sub = type_sub_count[type_id];
        if (total_count - already_sub < sub_count) {
          result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
          result.failed_index = static_cast<int32_t>(i);
          return checked_request;
        }
        type_sub_count[type_id] = already_sub + sub_count;
      }
    }

    // 子类额外检查
    int32_t extra_ret = on_check_sub(config_group, req);
    if (extra_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = extra_ret;
      result.failed_index = static_cast<int32_t>(i);
      return checked_request;
    }
  }

  return checked_request;
}

ItemGridOperationResult ItemGridAlgorithm::move(const ItemGridMoveCheckedRequest& checked_request) {
  if (checked_request.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return {checked_request.error_code};
  }

  ItemGridOperationResult result;
  const auto& request = checked_request.request;

  // ============================================================
  // 执行所有 Sub 操作
  // ============================================================
  for (const auto& sub_req : request.move_sub_entrys) {
    int64_t source_count = sub_req.entry->item_instance.item_basic().count();
    int64_t guid = sub_req.entry->item_instance.item_basic().guid();
    int32_t type_id = sub_req.entry->item_instance.item_basic().type_id();
    bool sub_is_whole = (source_count == sub_req.op_count);
    auto position_cfg = get_item_position_cfg(checked_request.config_group, sub_req.entry->item_instance.item_basic());

    if (sub_is_whole) {
      // 整体移除索引
      remove_entry_index(*position_cfg, sub_req.entry);
      remove_entry_from_group(sub_req.entry);
      sub_req.entry->item_instance.mutable_item_basic()->set_count(0);
      item_count_cache_[type_id] -= source_count;
      if (item_count_cache_[type_id] <= 0) {
        item_count_cache_.erase(type_id);
      }
      on_item_count_changed(type_id, sub_req.entry, guid, sub_req.position, source_count, 0,
                            get_cached_item_count(type_id), ItemGridOperationReason::kMoveSub);
      on_item_data_changed(sub_req.entry, ItemGridOperationReason::kMoveSub);
    } else {
      // 部分扣减
      sub_req.entry->item_instance.mutable_item_basic()->set_count(source_count - sub_req.op_count);
      item_count_cache_[type_id] -= sub_req.op_count;
      on_item_count_changed(type_id, sub_req.entry, guid, sub_req.position, source_count,
                            source_count - sub_req.op_count, item_count_cache_[type_id],
                            ItemGridOperationReason::kMoveSub);
      on_item_data_changed(sub_req.entry, ItemGridOperationReason::kMoveSub);
    }
  }

  // ============================================================
  // 执行所有 Add 操作
  // ============================================================
  for (const auto& add_req : request.move_add_entrys) {
    int32_t type_id = add_req.type_id;
    auto position_cfg = get_item_position_cfg(checked_request.config_group, add_req.entry->item_instance.item_basic());

    // 检查目标锚点位置是否有已有条目 (用于堆叠合入)
    auto target_it = position_index_.find(add_req.position);
    if (target_it != position_index_.end()) {
      // 合入已有条目 (堆叠)
      auto& target_entry = target_it->second;
      int64_t target_old = target_entry->item_instance.item_basic().count();
      target_entry->item_instance.mutable_item_basic()->set_count(target_old + add_req.op_count);
      item_count_cache_[type_id] += add_req.op_count;
      on_item_count_changed(type_id, target_entry, target_entry->item_instance.item_basic().guid(), add_req.position,
                            target_old, target_old + add_req.op_count, item_count_cache_[type_id],
                            ItemGridOperationReason::kMoveAdd);
      on_item_data_changed(target_entry, ItemGridOperationReason::kMoveAdd);
    } else {
      // 新建条目 (移入): 从 add_entry 复制数据
      PROJECT_NAMESPACE_ID::DItemInstance new_instance = add_req.entry->item_instance;
      new_instance.mutable_item_basic()->set_count(add_req.op_count);
      *new_instance.mutable_item_basic()->mutable_position() = add_req.goal_position;
      item_grid_entry_ptr_t new_entry = make_entry(std::move(new_instance));
      item_groups_[type_id].push_back(new_entry);
      add_entry_index(*position_cfg, new_entry);
      item_count_cache_[type_id] += add_req.op_count;
      on_item_count_changed(type_id, new_entry, new_entry->item_instance.item_basic().guid(), add_req.position, 0,
                            add_req.op_count, item_count_cache_[type_id], ItemGridOperationReason::kMoveAdd);
      on_item_data_changed(new_entry, ItemGridOperationReason::kMoveAdd);
    }
  }

  return result;
}

bool ItemGridAlgorithm::check_move_request(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    ItemGridMoveRequest& request, ItemGridMoveCheckedRequest& checked_request) const {
  auto& error_code = checked_request.error_code;

  // ============================================================
  // 1. 检查 Sub Entry 是否重复, op_count 合法, 填充 Helper 字段
  // ============================================================
  std::unordered_set<const ItemGridEntry*> sub_entry_set;
  std::unordered_map<int32_t, int64_t> type_count_delta;

  for (auto& sub_req : request.move_sub_entrys) {
    if (!sub_req.entry || sub_req.op_count <= 0) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      return false;
    }
    if (!sub_entry_set.insert(sub_req.entry.get()).second) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // entry 重复
      return false;
    }

    const auto& ref_basic = sub_req.entry->item_instance.item_basic();
    int32_t type_id = ref_basic.type_id();

    // op_count 不能超过 entry 的 count
    if (sub_req.op_count > ref_basic.count()) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
      return false;
    }

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      return false;
    }

    auto position_cfg = get_item_position_cfg(config_group, ref_basic);
    if (position_cfg == nullptr) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      return false;
    }

    // 填充 Helper 字段
    sub_req.position = extract_position(ref_basic.position().grid_position());
    sub_req.item_row = is_care_item_size() ? position_cfg->row_size() : 1;
    sub_req.item_col = is_care_item_size() ? position_cfg->column_size() : 1;
    if (sub_req.item_row <= 0) {
      sub_req.item_row = 1;
    }
    if (sub_req.item_col <= 0) {
      sub_req.item_col = 1;
    }

    type_count_delta[type_id] -= sub_req.op_count;
  }

  // ============================================================
  // 2. 检查 Add Entry 重复性, op_count, 填充 Helper, is_item_in_range, accumulation_limit, GUID
  // ============================================================
  std::unordered_set<const ItemGridEntry*> add_entry_set;
  std::unordered_set<int64_t> pending_guids;

  for (auto& add_req : request.move_add_entrys) {
    if (!add_req.entry || add_req.op_count <= 0) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      return false;
    }
    if (!add_entry_set.insert(add_req.entry.get()).second) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // entry 重复
      return false;
    }

    const auto& ref_basic = add_req.entry->item_instance.item_basic();
    int32_t type_id = ref_basic.type_id();

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      return false;
    }

    auto position_cfg = get_item_position_cfg(config_group, ref_basic);
    if (position_cfg == nullptr) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      return false;
    }

    int64_t accumulation_limit = position_cfg->accumulation_limit();
    if (accumulation_limit <= 0) {
      accumulation_limit = 1;
    }

    // 填充 Helper 字段
    add_req.type_id = type_id;
    add_req.position = extract_position(add_req.goal_position.grid_position());
    add_req.item_row = is_care_item_size() ? position_cfg->row_size() : 1;
    add_req.item_col = is_care_item_size() ? position_cfg->column_size() : 1;
    if (add_req.item_row <= 0) {
      add_req.item_row = 1;
    }
    if (add_req.item_col <= 0) {
      add_req.item_col = 1;
    }
    add_req.accumulation_limit = accumulation_limit;

    // 检查 Add is_item_in_range
    if (is_care_item_size()) {
      if (!is_item_in_range(add_req.position.x, add_req.position.y, add_req.item_row, add_req.item_col)) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OUT_OF_RANGE;
        return false;
      }
    }

    // 检查 op_count 不超过 accumulation_limit
    if (add_req.op_count > accumulation_limit) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
      return false;
    }

    bool is_paired = (sub_entry_set.count(add_req.entry.get()) > 0);
    if (!is_paired) {
      // Add-only (从外部移入): 检查 GUID 唯一性
      int64_t guid = ref_basic.guid();
      if (guid != 0) {
        if (guid_index_.count(guid) > 0 || !pending_guids.insert(guid).second) {
          error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID;
          return false;
        }
      }
    }

    // 跟踪类型数量变化 (add 增加)
    type_count_delta[type_id] += add_req.op_count;
  }

  // ============================================================
  // 3. 检查数量上限 (对净增加的类型调用 on_check_item_count_limit)
  // ============================================================
  for (const auto& delta_pair : type_count_delta) {
    if (delta_pair.second > 0) {
      int64_t current_total = get_cached_item_count(delta_pair.first);
      int32_t limit_ret = on_check_item_count_limit(delta_pair.first, current_total, delta_pair.second);
      if (limit_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
        error_code = limit_ret;
        return false;
      }
    }
  }

  return true;
}

ItemGridMoveCheckedRequest ItemGridAlgorithm::check_move(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    ItemGridMoveRequest& request) const {
  ItemGridMoveCheckedRequest checked_request{config_group, request};
  auto& error_code = checked_request.error_code;

  if (request.move_sub_entrys.empty() && request.move_add_entrys.empty()) {
    error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    return checked_request;
  }

  // ============================================================
  // Phase 0: 验证入参
  // ============================================================
  if (!check_move_request(config_group, request, checked_request)) {
    return checked_request;
  }

  // ============================================================
  // Phase 1 (Sub): 虚拟移除所有整体 Sub 的条目, 生成临时格子蒙版
  // ============================================================
  std::vector<std::vector<bool>> tmp_grid_flag;
  if (is_care_item_size()) {
    tmp_grid_flag = occupy_grid_flag_;
  }

  position_set_type removed_anchors;

  for (const auto& op : request.move_sub_entrys) {
    if (op.entry->item_instance.item_basic().count() > op.op_count) {
      continue;
    }

    removed_anchors.insert(op.position);
    if (is_care_item_size()) {
      for (int32_t dr = 0; dr < op.item_row; ++dr) {
        for (int32_t dc = 0; dc < op.item_col; ++dc) {
          int32_t r = op.position.y + dr;
          int32_t c = op.position.x + dc;
          tmp_grid_flag[static_cast<size_t>(r)][static_cast<size_t>(c)] = false;
        }
      }
    }
  }

  // ============================================================
  // Phase 2 (Add): 在临时数据上检查所有 Add 操作的可行性
  // ============================================================
  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_merge_extra;

  struct PendingNewAnchor {
    int32_t type_id = 0;
    int64_t count = 0;
    bool has_guid = false;
    int64_t accumulation_limit = 1;
  };
  std::unordered_map<ItemGridPosition, PendingNewAnchor, ItemGridPositionHash, ItemGridPositionEqualTo>
      pending_new_anchors;

  for (const auto& op : request.move_add_entrys) {
    // ---- 1. 检查目标锚点是否有未被移走的已有条目 (用于合入) ----
    auto existing_it = position_index_.find(op.position);
    bool has_unmoved_existing = (existing_it != position_index_.end() && removed_anchors.count(op.position) == 0);

    if (has_unmoved_existing) {
      // 目标位置有未被移走的已有条目, 只能合入
      const auto& target_basic = existing_it->second->item_instance.item_basic();

      if (target_basic.guid() != 0 || target_basic.type_id() != op.type_id) {
        // 目标位置有不同类型或带GUID的条目, 无法合入
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_MOVE_TARGET_OCCUPIED;
        return checked_request;
      }

      int64_t target_total = target_basic.count() + pending_merge_extra[op.position] + op.op_count;
      if (target_total > op.accumulation_limit) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
        return checked_request;
      }
      pending_merge_extra[op.position] += op.op_count;
      continue;
    }

    // ---- 2. 检查本批次是否已在此锚点创建了新条目 ----
    auto pending_it = pending_new_anchors.find(op.position);
    if (pending_it != pending_new_anchors.end()) {
      if (pending_it->second.has_guid || pending_it->second.type_id != op.type_id) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
        return checked_request;
      }
      int64_t total = pending_it->second.count + op.op_count;
      if (total > pending_it->second.accumulation_limit) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
        return checked_request;
      }
      pending_it->second.count = total;
      continue;
    }

    // ---- 3. 全新位置 — 检查放置可行性 ----
    if (is_care_item_size()) {
      for (int32_t dr = 0; dr < op.item_row; ++dr) {
        for (int32_t dc = 0; dc < op.item_col; ++dc) {
          if (tmp_grid_flag[static_cast<size_t>(op.position.y + dr)][static_cast<size_t>(op.position.x + dc)]) {
            error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
            return checked_request;
          }
          tmp_grid_flag[static_cast<size_t>(op.position.y + dr)][static_cast<size_t>(op.position.x + dc)] = true;
        }
      }
    }

    pending_new_anchors[op.position] =
        PendingNewAnchor{op.type_id, static_cast<int64_t>(op.op_count),
                         op.entry->item_instance.item_basic().guid() != 0, op.accumulation_limit};
  }

  return checked_request;
}

// ============================================================
// Load / Foreach
// ============================================================

bool ItemGridAlgorithm::load(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                             const PROJECT_NAMESPACE_ID::DItemInstance& item_instance) {
  constexpr auto kReason = ItemGridOperationReason::kLoad;

  const auto& item_basic = item_instance.item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t add_count = item_basic.count();
  int64_t guid = item_basic.guid();

  // 基础合法性校验
  if (!is_item_valid(config_group, item_basic)) {
    return false;
  }

  // GUID 唯一性
  if (guid != 0 && guid_index_.count(guid) > 0) {
    return false;
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr) {
    return false;
  }

  // 数量上限检查
  int64_t current_total = get_cached_item_count(type_id);
  int32_t limit_ret = on_check_item_count_limit(type_id, current_total, add_count);
  if (limit_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return false;
  }

  // ------ 不占格道具 ------
  if (!item_type_config->need_occupy_the_grid) {
    if (!item_type_config->need_guid) {
      // 无GUID: 合并到已有 entry
      auto group_it = item_groups_.find(type_id);
      if (group_it != item_groups_.end() && !group_it->second.empty()) {
        auto& existing = group_it->second.front();
        int64_t old_count = existing->item_instance.item_basic().count();
        existing->item_instance.mutable_item_basic()->set_count(old_count + add_count);
        item_count_cache_[type_id] += add_count;
        on_item_count_changed(type_id, existing, 0, ItemGridPosition{}, old_count, old_count + add_count,
                              item_count_cache_[type_id], kReason);
        on_item_data_changed(existing, kReason);
        return true;
      }
    }

    // 有GUID 或 首次添加
    PROJECT_NAMESPACE_ID::DItemInstance new_instance = item_instance;
    item_grid_entry_ptr_t entry = make_entry(std::move(new_instance));
    item_groups_[type_id].push_back(entry);
    if (guid != 0) {
      guid_index_[guid] = entry;
    }
    item_count_cache_[type_id] += add_count;
    on_item_count_changed(type_id, entry, guid, ItemGridPosition{}, 0, add_count, item_count_cache_[type_id], kReason);
    on_item_data_changed(entry, kReason);
    return true;
  }

  // ------ 占格道具 ------
  auto position_cfg = get_item_position_cfg(config_group, item_basic);
  if (position_cfg == nullptr) {
    return false;
  }

  // 不检查堆叠上限 !!
  ItemGridPosition target_pos = extract_position(item_basic.position().grid_position());

  // 堆叠到已有条目
  auto pos_it = position_index_.find(target_pos);
  if (pos_it != position_index_.end()) {
    const auto& existing_basic = pos_it->second->item_instance.item_basic();
    if (guid != 0 || existing_basic.guid() != 0 || existing_basic.type_id() != type_id) {
      return false;
    }
    auto& existing = pos_it->second;
    int64_t old_count = existing->item_instance.item_basic().count();
    existing->item_instance.mutable_item_basic()->set_count(old_count + add_count);
    item_count_cache_[type_id] += add_count;
    on_item_count_changed(type_id, existing, existing->item_instance.item_basic().guid(), target_pos, old_count,
                          old_count + add_count, item_count_cache_[type_id], kReason);
    on_item_data_changed(existing, kReason);
    return true;
  }

  // 新建条目: 检查格子占用 (is_care_item_size 模式)
  if (is_care_item_size()) {
    int32_t item_row = position_cfg->row_size();
    int32_t item_col = position_cfg->column_size();
    if (item_row <= 0 || item_col <= 0) {
      return false;
    }
    if (!is_item_in_range(target_pos.x, target_pos.y, item_row, item_col)) {
      return false;
    }
    if (check_collision(target_pos.x, target_pos.y, item_row, item_col)) {
      return false;
    }
  }

  // 放入
  PROJECT_NAMESPACE_ID::DItemInstance new_instance = item_instance;
  apply_position(*new_instance.mutable_item_basic()->mutable_position()->mutable_grid_position(), target_pos);

  item_grid_entry_ptr_t entry = make_entry(std::move(new_instance));
  item_groups_[type_id].push_back(entry);
  add_entry_index(*position_cfg, entry);
  item_count_cache_[type_id] += add_count;
  on_item_count_changed(type_id, entry, entry->item_instance.item_basic().guid(), target_pos, 0, add_count,
                        item_count_cache_[type_id], kReason);
  on_item_data_changed(entry, kReason);
  return true;
}

void ItemGridAlgorithm::foreach (std::function<bool(const PROJECT_NAMESPACE_ID::DItemInstance&)> fn) const {
  for (const auto& group_pair : item_groups_) {
    for (const auto& entry : group_pair.second) {
      if (entry) {
        if (!fn(entry->item_instance)) {
          return;
        }
      }
    }
  }
}

void ItemGridAlgorithm::apply_entries(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const ::google::protobuf::RepeatedField<uint64_t>& remove_entry_ids,
    const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry>& update_entries) {
  // ============================================================
  // Phase 1: 按 entry_id 删除
  // ============================================================
  for (uint64_t remove_id : remove_entry_ids) {
    // 遍历所有 group 查找匹配 entry_id 的条目
    item_grid_entry_ptr_t found;
    for (auto& group_pair : item_groups_) {
      for (auto& entry : group_pair.second) {
        if (entry && entry->entry_id == remove_id) {
          found = entry;
          break;
        }
      }
      if (found) {
        break;
      }
    }
    if (!found) {
      continue;
    }

    int32_t type_id = found->item_instance.item_basic().type_id();
    int64_t old_count = found->item_instance.item_basic().count();
    int64_t guid = found->item_instance.item_basic().guid();
    ItemGridPosition pos = extract_position(found->item_instance.item_basic().position().grid_position());

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config && item_type_config->need_occupy_the_grid) {
      auto position_cfg = get_item_position_cfg(config_group, found->item_instance.item_basic());
      if (position_cfg) {
        remove_entry_index(*position_cfg, found);
      }
    } else {
      if (guid != 0) {
        guid_index_.erase(guid);
      }
    }

    remove_entry_from_group(found);
    found->item_instance.mutable_item_basic()->set_count(0);
    item_count_cache_[type_id] -= old_count;
    if (item_count_cache_[type_id] <= 0) {
      item_count_cache_.erase(type_id);
    }

    on_item_count_changed(type_id, found, guid, pos, old_count, 0, get_cached_item_count(type_id),
                          ItemGridOperationReason::kApplyRemove);
    on_item_data_changed(found, ItemGridOperationReason::kApplyRemove);
  }

  // ============================================================
  // Phase 2: 按 entry_id 新增或更新
  // ============================================================
  for (const auto& update : update_entries) {
    const auto& item_basic = update.instance().item_basic();
    int32_t type_id = item_basic.type_id();
    int64_t new_count = item_basic.count();
    int64_t guid = item_basic.guid();

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr) {
      continue;
    }

    // 查找已有 entry (按 entry_id)
    item_grid_entry_ptr_t existing;
    if (update.entry_id() != 0) {
      for (auto& group_pair : item_groups_) {
        for (auto& entry : group_pair.second) {
          if (entry && entry->entry_id == update.entry_id()) {
            existing = entry;
            break;
          }
        }
        if (existing) {
          break;
        }
      }
    }

    if (existing) {
      // --- 更新已有 entry ---
      int64_t old_count = existing->item_instance.item_basic().count();
      ItemGridPosition old_pos = extract_position(existing->item_instance.item_basic().position().grid_position());
      ItemGridPosition new_pos = extract_position(item_basic.position().grid_position());

      // 位置变化: 先移除旧索引再添加新索引
      bool position_changed = (old_pos != new_pos);
      if (position_changed && item_type_config->need_occupy_the_grid) {
        auto position_cfg = get_item_position_cfg(config_group, existing->item_instance.item_basic());
        if (position_cfg) {
          remove_entry_index(*position_cfg, existing);
        }
      }

      // 覆盖 item_instance
      existing->item_instance = update.instance();

      if (position_changed && item_type_config->need_occupy_the_grid) {
        auto position_cfg = get_item_position_cfg(config_group, existing->item_instance.item_basic());
        if (position_cfg) {
          add_entry_index(*position_cfg, existing);
        }
      }

      // GUID 索引刷新
      int64_t old_guid = existing->item_instance.item_basic().guid();
      if (old_guid != guid) {
        if (old_guid != 0) guid_index_.erase(old_guid);
        if (guid != 0) guid_index_[guid] = existing;
      }

      // 更新 count cache
      int64_t count_delta = new_count - old_count;
      if (count_delta != 0) {
        item_count_cache_[type_id] += count_delta;
        if (item_count_cache_[type_id] <= 0) {
          item_count_cache_.erase(type_id);
        }
      }

      on_item_count_changed(type_id, existing, guid, new_pos, old_count, new_count, get_cached_item_count(type_id),
                            ItemGridOperationReason::kApplyUpdate);
      on_item_data_changed(existing, ItemGridOperationReason::kApplyUpdate);
    } else {
      // --- 新增 entry ---
      item_grid_entry_ptr_t new_entry = make_entry(PROJECT_NAMESPACE_ID::DItemInstance(update.instance()));
      // 如果 update 携带了 entry_id, 强制覆盖 make_entry 分配的值
      if (update.entry_id() != 0) {
        new_entry->entry_id = update.entry_id();
      }

      item_groups_[type_id].push_back(new_entry);

      if (item_type_config->need_occupy_the_grid) {
        auto position_cfg = get_item_position_cfg(config_group, item_basic);
        if (position_cfg) {
          add_entry_index(*position_cfg, new_entry);
        }
      } else {
        if (guid != 0) {
          guid_index_[guid] = new_entry;
        }
      }

      item_count_cache_[type_id] += new_count;
      ItemGridPosition pos = extract_position(item_basic.position().grid_position());

      on_item_count_changed(type_id, new_entry, guid, pos, 0, new_count, get_cached_item_count(type_id),
                            ItemGridOperationReason::kApplyUpdate);
      on_item_data_changed(new_entry, ItemGridOperationReason::kApplyUpdate);
    }
  }
}

// ============================================================
// Clear / Get / 查询
// ============================================================

void ItemGridAlgorithm::clear() {
  item_groups_.clear();
  position_index_.clear();
  guid_index_.clear();
  item_count_cache_.clear();

  for (auto& row : occupy_grid_flag_) {
    std::fill(row.begin(), row.end(), false);
  }
}

item_grid_entry_ptr_t ItemGridAlgorithm::get(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
  ItemGridPosition grid_pos = extract_position(position);
  auto it = position_index_.find(grid_pos);
  if (it != position_index_.end()) {
    return it->second;
  }
  return nullptr;
}

item_grid_entry_ptr_t ItemGridAlgorithm::get_by_guid(int64_t guid) const {
  auto it = guid_index_.find(guid);
  if (it != guid_index_.end()) {
    return it->second;
  }
  return nullptr;
}

const ItemGridAlgorithm::item_group_type* ItemGridAlgorithm::get_group(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  if (it != item_groups_.end()) {
    return &it->second;
  }
  return nullptr;
}

int64_t ItemGridAlgorithm::get_item_count(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  if (it == item_groups_.end()) {
    return 0;
  }

  int64_t total = 0;
  for (const auto& entry : it->second) {
    if (entry) {
      total += entry->item_instance.item_basic().count();
    }
  }
  return total;
}

bool ItemGridAlgorithm::is_empty() const { return item_groups_.empty(); }

const ItemGridAlgorithm::item_group_map_type& ItemGridAlgorithm::get_all_groups() const { return item_groups_; }

const std::vector<std::vector<bool>>& ItemGridAlgorithm::get_occupy_grid_flag() const { return occupy_grid_flag_; }

int32_t ItemGridAlgorithm::get_row_size() const { return row_size_; }

int32_t ItemGridAlgorithm::get_column_size() const { return column_size_; }

// ============================================================
// 位置转换辅助
// ============================================================

ItemGridPosition ItemGridAlgorithm::extract_position(const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
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
      position.mutable_character_equipment()->set_slot_idx(static_cast<PROJECT_NAMESPACE_ID::EnEquipmentSlot>(grid_pos.x));
      break;
    }
    default:
      break;
  }
}

bool ItemGridAlgorithm::find_positions_for_basics(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition>& out_positions) const {
  out_positions.clear();
  out_positions.reserve(basics.size());

  // 批次内格子预留副本 (care_item_size 模式)：记录已分配格子，避免批次内冲突，不修改实际背包数据
  std::vector<std::vector<bool>> reserved;
  if (is_care_item_size()) {
    reserved = occupy_grid_flag_;
  }

  // -----------------------------------------------------------------------
  // 优化：按物品尺寸 (rows, cols) 记录线性扫描游标。
  //
  // key: (item_rows as uint32_t) << 32 | (item_cols as uint32_t)
  // value: (last_placed_y, last_placed_x)   ← 下次从此处开始
  // -----------------------------------------------------------------------
  std::unordered_map<uint64_t, std::pair<int32_t, int32_t>> size_scan_cursors;

  // 复用临时 DItemInstance（避免循环体内多次分配），用于构造 on_check_add 入参
  PROJECT_NAMESPACE_ID::DItemInstance tmp_inst;

  for (const auto& basic : basics) {
    auto* item_type_cfg = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
    if (!item_type_cfg) {
      return false;
    }

    // 非占格道具（货币/虚拟道具）：不需要格子位置，输出默认值
    if (!item_type_cfg->need_occupy_the_grid) {
      out_positions.push_back(PROJECT_NAMESPACE_ID::DItemGridPosition{});
      continue;
    }

    // 获取物品尺寸配置
    auto* pos_cfg = get_item_position_cfg(config_group, basic);
    if (!pos_cfg) {
      return false;
    }
    const int32_t item_rows = is_care_item_size() ? pos_cfg->row_size() : 1;
    const int32_t item_cols = is_care_item_size() ? pos_cfg->column_size() : 1;

    // 辅助：构造含候选位置的临时请求并通过 on_check_add 做额外校验
    auto check_pos_ok = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos) -> bool {
      tmp_inst.Clear();
      *tmp_inst.mutable_item_basic() = basic;
      *tmp_inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = cand_pos;
      ItemGridAddRequest check_req{&tmp_inst};
      return on_check_add(config_group, check_req) == PROJECT_NAMESPACE_ID::EN_SUCCESS;
    };

    // ---- non-care 模式（装备槽等）：完全委托给子类钩子，不做格子扫描 ----
    if (!is_care_item_size()) {
      PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
      if (on_find_position_for_non_care(config_group, basic, out_pos) && check_pos_ok(out_pos)) {
        out_positions.push_back(std::move(out_pos));
      } else {
        return false;  // 子类无法确定位置
      }
      continue;
    }

    // ---- care_item_size 模式：三段策略 + on_check_add 校验 ----

    // 辅助：检查 (x,y) 起点的 item_cols×item_rows 区域是否在 reserved 中全部空闲
    auto is_free_in_reserved = [&](int32_t x, int32_t y) -> bool {
      if (!is_item_in_range(x, y, item_rows, item_cols)) {
        return false;
      }
      for (int32_t r = 0; r < item_rows; ++r) {
        for (int32_t c = 0; c < item_cols; ++c) {
          if (reserved[static_cast<size_t>(y + r)][static_cast<size_t>(x + c)]) {
            return false;
          }
        }
      }
      return true;
    };

    // 辅助：在 reserved 中标记 (x,y) 起点的区域为已占用
    auto mark_reserved = [&](int32_t x, int32_t y) {
      for (int32_t r = 0; r < item_rows; ++r) {
        for (int32_t c = 0; c < item_cols; ++c) {
          reserved[static_cast<size_t>(y + r)][static_cast<size_t>(x + c)] = true;
        }
      }
    };

    bool placed = false;

    // 策略 1：若 basic 自带首选位置，优先尝试
    if (!placed) {
      ItemGridPosition preferred = extract_position(basic.position().grid_position());
      if (preferred.x >= 0 && preferred.y >= 0 && is_free_in_reserved(preferred.x, preferred.y)) {
        PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
        apply_position(out_pos, preferred);
        if (check_pos_ok(out_pos)) {
          out_positions.push_back(out_pos);
          mark_reserved(preferred.x, preferred.y);
          placed = true;
        }
      }
    }

    // 策略 2：堆叠到已有的同类型无GUID条目（堆叠上限>1且剩余容量足够）
    if (!placed && basic.guid() == 0 && pos_cfg->accumulation_limit() > 1) {
      for (const auto& kv : position_index_) {
        const auto& eb = kv.second->item_instance.item_basic();
        if (eb.type_id() == basic.type_id() && eb.guid() == 0) {
          int64_t remaining = static_cast<int64_t>(pos_cfg->accumulation_limit()) - eb.count();
          if (remaining >= basic.count()) {
            PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
            apply_position(out_pos, kv.first);
            if (check_pos_ok(out_pos)) {
              out_positions.push_back(std::move(out_pos));
              placed = true;
              break;
            }
          }
        }
      }
    }

    // 策略 3：游标扫描背包寻找第一个空闲格子，通过 on_check_add 校验
    if (!placed) {
      const uint64_t size_key = (static_cast<uint64_t>(static_cast<uint32_t>(item_rows)) << 32) |
                                static_cast<uint64_t>(static_cast<uint32_t>(item_cols));
      int32_t start_y = 0;
      int32_t start_x = 0;
      auto cursor_it = size_scan_cursors.find(size_key);
      if (cursor_it != size_scan_cursors.end()) {
        start_y = cursor_it->second.first;
        start_x = cursor_it->second.second;
      }

      for (int32_t y = start_y; y <= row_size_ - item_rows && !placed; ++y) {
        const int32_t x_begin = (y == start_y) ? start_x : 0;
        for (int32_t x = x_begin; x <= column_size_ - item_cols && !placed; ++x) {
          if (is_free_in_reserved(x, y)) {
            ItemGridPosition pos{x, y};
            PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
            apply_position(out_pos, pos);
            if (check_pos_ok(out_pos)) {
              out_positions.push_back(out_pos);
              mark_reserved(x, y);
              // 更新游标：下次同尺寸物品从此位置继续（该位置已标记，扫描会自然跳过）
              size_scan_cursors[size_key] = {y, x};
              placed = true;
            }
          }
        }
      }
    }

    if (!placed) {
      return false;  // 背包已满，无法放置该道具
    }
  }

  return true;
}

// ============================================================
// 虚函数钩子 — 默认实现
// ============================================================

const PROJECT_NAMESPACE_ID::DItemPositionCfg* ItemGridAlgorithm::get_item_position_cfg(
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

int32_t ItemGridAlgorithm::on_check_add(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const ItemGridAddRequest& /*request*/) const {
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

int32_t ItemGridAlgorithm::on_check_sub(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const ItemGridSubRequest& /*request*/) const {
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

int32_t ItemGridAlgorithm::on_check_item_count_limit(int32_t /*type_id*/, int64_t /*current_count*/,
                                                     int64_t /*add_count*/) const {
  // 默认不限制, 子类按需覆盖
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

bool ItemGridAlgorithm::on_find_position_for_non_care(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const PROJECT_NAMESPACE_ID::DItemBasic& /*basic*/,
    PROJECT_NAMESPACE_ID::DItemGridPosition& /*out_pos*/) const {
  // 默认无法确定位置, 子类按需覆盖 (如装备槽按 type_id 映射 slot_idx)
  return false;
}

void ItemGridAlgorithm::on_item_count_changed(int32_t /*type_id*/, const item_grid_entry_ptr_t& /*entry*/,
                                              int64_t /*guid*/, const ItemGridPosition& /*position*/,
                                              int64_t /*old_count*/, int64_t /*new_count*/,
                                              int64_t /*type_total_count*/, ItemGridOperationReason /*reason*/) {
  // 默认空实现, 子类按需覆盖
}

void ItemGridAlgorithm::on_item_data_changed(const item_grid_entry_ptr_t& /*entry*/,
                                             ItemGridOperationReason /*reason*/) {
  // 默认空实现, 子类按需覆盖
}

int64_t ItemGridAlgorithm::get_cached_item_count(int32_t type_id) const {
  auto it = item_count_cache_.find(type_id);
  return (it != item_count_cache_.end()) ? it->second : 0;
}

item_grid_entry_ptr_t ItemGridAlgorithm::make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance) const {
  auto entry = ::excel::excel_config_type_traits::make_shared<ItemGridEntry>(std::move(instance));
  entry->entry_id = next_entry_id_++;
  return entry;
}

// ============================================================
// 内部实现
// ============================================================

bool ItemGridAlgorithm::is_grid_position_valid(const ItemGridPosition& position) const {
  if (position.x < 0 || position.y < 0) {
    return false;
  }
  if (position.x >= column_size_ || position.y >= row_size_) {
    return false;
  }
  return true;
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
    auto position_cfg = get_item_position_cfg(config_group, basic);
    if (position_cfg == nullptr) {
      return false;
    }
  }

  if (item_type_config->need_guid) {
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
  if (!is_care_item_size()) {
    return true;
  }
  return x >= 0 && y >= 0 && x + item_col_size <= column_size_ && y + item_row_size <= row_size_;
}

bool ItemGridAlgorithm::check_collision(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size) const {
  if (!is_care_item_size()) {
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
      if (occupy_grid_flag_[static_cast<size_t>(r)][static_cast<size_t>(c)]) {
        return true;
      }
    }
  }
  return false;
}

void ItemGridAlgorithm::set_grid_flag(int32_t x, int32_t y, int32_t item_row_size, int32_t item_col_size,
                                      bool occupied) {
  if (!is_care_item_size()) {
    return;
  }

  for (int32_t dr = 0; dr < item_row_size; ++dr) {
    for (int32_t dc = 0; dc < item_col_size; ++dc) {
      int32_t r = y + dr;
      int32_t c = x + dc;
      if (r >= 0 && r < row_size_ && c >= 0 && c < column_size_) {
        occupy_grid_flag_[static_cast<size_t>(r)][static_cast<size_t>(c)] = occupied;
      }
    }
  }
}

item_grid_entry_ptr_t ItemGridAlgorithm::find_entry(const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
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
      return it->second.front();
    }
  }
  return nullptr;
}

void ItemGridAlgorithm::remove_entry_index(const PROJECT_NAMESPACE_ID::DItemPositionCfg& position_cfg,
                                           const item_grid_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance.item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance.item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    position_index_.erase(pos);
  }

  if (is_care_item_size() && item_type_config->need_occupy_the_grid) {
    int32_t item_row = position_cfg.row_size();
    int32_t item_col = position_cfg.column_size();
    set_grid_flag(pos.x, pos.y, item_row, item_col, false);
  }

  int64_t guid = entry->item_instance.item_basic().guid();
  if (guid != 0) {
    guid_index_.erase(guid);
  }
}

void ItemGridAlgorithm::remove_entry_from_group(const item_grid_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  int32_t type_id = entry->item_instance.item_basic().type_id();
  auto group_it = item_groups_.find(type_id);
  if (group_it != item_groups_.end()) {
    group_it->second.remove(entry);
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

  ItemGridPosition pos = extract_position(entry->item_instance.item_basic().position().grid_position());
  position_index_[pos] = entry;

  if (is_care_item_size()) {
    int32_t item_row = position_cfg.row_size();
    int32_t item_col = position_cfg.column_size();
    set_grid_flag(pos.x, pos.y, item_row, item_col, true);
  }

  int64_t guid = entry->item_instance.item_basic().guid();
  if (guid != 0) {
    guid_index_[guid] = entry;
  }
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
