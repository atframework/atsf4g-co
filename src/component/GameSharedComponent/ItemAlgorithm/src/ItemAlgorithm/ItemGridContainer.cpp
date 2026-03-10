// Copyright 2025 atframework

#include "ItemAlgorithm/ItemGridContainer.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <vector>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// ItemGridContainer
// ============================================================

ItemGridContainer::ItemGridContainer() = default;
ItemGridContainer::~ItemGridContainer() = default;

// ============================================================
// Add — check_add: 按 DItemPosition 分组委托各 Grid 的 check_add
// ============================================================

ItemGridContainerAddCheckedRequest ItemGridContainer::check_add(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<ItemGridAddRequest>& requests) {
  ItemGridContainerAddCheckedRequest checked{config_group, requests};

  // 分组: Grid* -> (原始索引列表, 请求子集)
  struct GridBatch {
    ItemGridAlgorithm* grid = nullptr;
    std::vector<size_t> original_indices;
    std::vector<ItemGridAddRequest> sub_requests;
  };
  std::unordered_map<ItemGridAlgorithm*, GridBatch> batches;

  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& req = requests[i];
    if (req.item_instance == nullptr) {
      checked.result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      checked.result.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    ItemGridAlgorithm* grid = select_grid(req.item_instance->item_basic().position());
    if (grid == nullptr) {
      checked.result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      checked.result.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    auto& batch = batches[grid];
    batch.grid = grid;
    batch.original_indices.push_back(i);
    batch.sub_requests.push_back(req);
  }

  // Check 阶段 — 全部 Grid 检查通过后保存
  checked.grid_data.reserve(batches.size());

  for (auto& pair : batches) {
    auto& batch = pair.second;
    auto grid_checked = batch.grid->check_add(config_group, batch.sub_requests);
    if (grid_checked.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      checked.result.error_code = grid_checked.result.error_code;
      int32_t local_idx = grid_checked.result.failed_index;
      if (local_idx >= 0 && static_cast<size_t>(local_idx) < batch.original_indices.size()) {
        checked.result.failed_index = static_cast<int32_t>(batch.original_indices[static_cast<size_t>(local_idx)]);
      } else {
        checked.result.failed_index = -1;
      }
      return checked;
    }
    ItemGridContainerAddCheckedRequest::PerGridData gd;
    gd.grid = batch.grid;
    gd.sub_requests = std::move(batch.sub_requests);
    gd.original_indices = std::move(batch.original_indices);
    checked.grid_data.push_back(std::move(gd));
  }

  return checked;
}

// ============================================================
// Add — add: 基于 checked_request 执行实际添加
// ============================================================

ItemGridOperationResult ItemGridContainer::add(const ItemGridContainerAddCheckedRequest& checked_request) {
  ItemGridOperationResult result;

  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    result.error_code = checked_request.result.error_code;
    result.failed_index = checked_request.result.failed_index;
    return result;
  }

  for (const auto& gd : checked_request.grid_data) {
    // 构建 Grid 层 checked request
    ItemGridAddCheckedRequest grid_checked{checked_request.config_group, gd.sub_requests};
    auto exec_result = gd.grid->add(grid_checked);
    if (exec_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = exec_result.error_code;
      int32_t local_idx = exec_result.failed_index;
      if (local_idx >= 0 && static_cast<size_t>(local_idx) < gd.original_indices.size()) {
        result.failed_index = static_cast<int32_t>(gd.original_indices[static_cast<size_t>(local_idx)]);
      } else {
        result.failed_index = -1;
      }
      return result;
    }
  }

  return result;
}

// ============================================================
// Sub — check_sub: 按 DItemPosition 分组委托各 Grid 的 check_sub
// ============================================================

ItemGridContainerSubCheckedRequest ItemGridContainer::check_sub(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<ItemGridSubRequest>& requests) {
  ItemGridContainerSubCheckedRequest checked{config_group, requests};

  struct GridBatch {
    ItemGridAlgorithm* grid = nullptr;
    std::vector<size_t> original_indices;
    std::vector<ItemGridSubRequest> sub_requests;
  };
  std::unordered_map<ItemGridAlgorithm*, GridBatch> batches;

  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& req = requests[i];
    if (req.item_basic == nullptr) {
      checked.result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      checked.result.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    ItemGridAlgorithm* grid = select_grid(req.item_basic->position());
    if (grid == nullptr) {
      checked.result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      checked.result.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    auto& batch = batches[grid];
    batch.grid = grid;
    batch.original_indices.push_back(i);
    batch.sub_requests.push_back(req);
  }

  // Check 阶段 — 全部 Grid 检查通过后保存
  checked.grid_data.reserve(batches.size());

  for (auto& pair : batches) {
    auto& batch = pair.second;
    auto grid_checked = batch.grid->check_sub(config_group, batch.sub_requests);
    if (grid_checked.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      checked.result.error_code = grid_checked.result.error_code;
      int32_t local_idx = grid_checked.result.failed_index;
      if (local_idx >= 0 && static_cast<size_t>(local_idx) < batch.original_indices.size()) {
        checked.result.failed_index = static_cast<int32_t>(batch.original_indices[static_cast<size_t>(local_idx)]);
      } else {
        checked.result.failed_index = -1;
      }
      return checked;
    }
    ItemGridContainerSubCheckedRequest::PerGridData gd;
    gd.grid = batch.grid;
    gd.sub_requests = std::move(batch.sub_requests);
    gd.original_indices = std::move(batch.original_indices);
    checked.grid_data.push_back(std::move(gd));
  }

  return checked;
}

// ============================================================
// Sub — sub: 基于 checked_request 执行实际扣减
// ============================================================

ItemGridOperationResult ItemGridContainer::sub(const ItemGridContainerSubCheckedRequest& checked_request) {
  ItemGridOperationResult result;

  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    result.error_code = checked_request.result.error_code;
    result.failed_index = checked_request.result.failed_index;
    return result;
  }

  for (const auto& gd : checked_request.grid_data) {
    // 构建 Grid 层 checked request
    ItemGridSubCheckedRequest grid_checked{checked_request.config_group, gd.sub_requests};
    auto exec_result = gd.grid->sub(grid_checked);
    if (exec_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = exec_result.error_code;
      int32_t local_idx = exec_result.failed_index;
      if (local_idx >= 0 && static_cast<size_t>(local_idx) < gd.original_indices.size()) {
        result.failed_index = static_cast<int32_t>(gd.original_indices[static_cast<size_t>(local_idx)]);
      } else {
        result.failed_index = -1;
      }
      return result;
    }
  }

  return result;
}

// ============================================================
// Move — check_move: 验证参数与源条目, 构建每个 Grid 的 MoveRequest, 调用 Grid::check_move
// ============================================================

ItemGridContainerMoveCheckedRequest ItemGridContainer::check_move(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<ItemGridContainerMoveRequest>& requests) {
  ItemGridContainerMoveCheckedRequest checked{config_group, requests};

  if (requests.empty()) {
    return checked;
  }

  // ============================================================
  // Phase 0: 分类请求, 构建每个 Grid 的 ItemGridMoveRequest
  // ============================================================
  struct GridBuilder {
    ItemGridAlgorithm* grid = nullptr;
    ItemGridMoveRequest move_request;
    std::vector<size_t> original_indices;
  };
  std::unordered_map<ItemGridAlgorithm*, GridBuilder> builders;

  // 跟踪同一源条目的剩余数量 (判断整体/部分移动, 决定GUID)
  struct SourceEntryTracker {
    item_grid_entry_ptr_t entry;
    int64_t remaining = 0;
  };
  std::unordered_map<const ItemGridEntry*, SourceEntryTracker> source_tracking;

  // 跟踪同一 Grid 上同一源条目的 Sub 合并索引: (grid, entry) → move_sub_entrys 的索引
  struct GridEntryKey {
    ItemGridAlgorithm* grid = nullptr;
    const ItemGridEntry* entry = nullptr;

    bool operator==(const GridEntryKey& other) const noexcept {
      return grid == other.grid && entry == other.entry;
    }
  };
  struct GridEntryKeyHash {
    size_t operator()(const GridEntryKey& key) const noexcept {
      size_t h = std::hash<void*>{}(static_cast<void*>(key.grid));
      h ^= std::hash<const void*>{}(static_cast<const void*>(key.entry)) + size_t{0x9e3779b9} + (h << 6) + (h >> 2);
      return h;
    }
  };
  std::unordered_map<GridEntryKey, size_t, GridEntryKeyHash> sub_merge_index;

  for (size_t i = 0; i < requests.size(); ++i) {
    const auto& req = requests[i];

    // 容器层基本参数校验
    int32_t type_id = req.source_item_basic.type_id();
    int64_t move_count = req.source_item_basic.count();
    if (move_count <= 0 || type_id == 0) {
      checked.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      checked.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    ItemGridAlgorithm* source_grid = select_grid(req.source_item_basic.position());
    ItemGridAlgorithm* target_grid = select_grid(req.target_position);

    if (source_grid == nullptr || target_grid == nullptr) {
      checked.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      checked.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    // 查找源条目
    item_grid_entry_ptr_t source_entry = source_grid->find_entry(req.source_item_basic);
    if (!source_entry) {
      checked.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      checked.failed_index = static_cast<int32_t>(i);
      return checked;
    }

    // 容器层校验: 移动数量不超过剩余持有数量
    auto& tracker = source_tracking[source_entry.get()];
    if (!tracker.entry) {
      tracker.entry = source_entry;
      tracker.remaining = source_entry->item_instance.item_basic().count();
    }
    if (move_count > tracker.remaining) {
      checked.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
      checked.failed_index = static_cast<int32_t>(i);
      return checked;
    }
    bool is_whole_move = (move_count == tracker.remaining);
    tracker.remaining -= move_count;

    int32_t op_count = static_cast<int32_t>(move_count);

    // 检查源位置与目标位置是否相同, 相同直接跳过
    if (source_grid == target_grid) {
      ItemGridPosition source_pos = source_grid->extract_position(
          req.source_item_basic.position().grid_position());
      ItemGridPosition target_pos = target_grid->extract_position(
          req.target_position.grid_position());
      if (source_pos == target_pos) {
        continue;
      }
    }

    if (source_grid == target_grid) {
      // ---- 同Grid Move: sub + add 使用同一源条目, 合并同 Entry 的 Sub ----
      auto& builder = builders[source_grid];
      builder.grid = source_grid;
      builder.original_indices.push_back(i);

      // 合并同 Entry 的 Sub Request
      GridEntryKey sub_key{source_grid, source_entry.get()};
      auto sub_it = sub_merge_index.find(sub_key);
      if (sub_it != sub_merge_index.end()) {
        // 已有该 Entry 的 Sub, 合并 op_count
        builder.move_request.move_sub_entrys[sub_it->second].op_count += op_count;
      } else {
        sub_merge_index[sub_key] = builder.move_request.move_sub_entrys.size();
        builder.move_request.move_sub_entrys.push_back({source_entry, op_count});
      }

      // Add Request 不合并, 每次都新增; 需构造独立 entry 避免 Grid 层去重检查失败
      {
        PROJECT_NAMESPACE_ID::DItemInstance add_instance = source_entry->item_instance;
        add_instance.mutable_item_basic()->set_count(op_count);
        if (!is_whole_move) {
          add_instance.mutable_item_basic()->set_guid(0);
        }
        *add_instance.mutable_item_basic()->mutable_position() = req.target_position;

        auto add_entry =
            ::excel::excel_config_type_traits::make_shared<ItemGridEntry>(std::move(add_instance));
        checked.constructed_entries.push_back(add_entry);
        builder.move_request.move_add_entrys.push_back({add_entry, req.target_position, op_count});
      }
    } else {
      // ---- 跨Grid Move ----
      // 1. 源Grid: Sub-only — 合并同 Entry 的 Sub
      {
        auto& builder = builders[source_grid];
        builder.grid = source_grid;
        builder.original_indices.push_back(i);

        GridEntryKey sub_key{source_grid, source_entry.get()};
        auto sub_it = sub_merge_index.find(sub_key);
        if (sub_it != sub_merge_index.end()) {
          builder.move_request.move_sub_entrys[sub_it->second].op_count += op_count;
        } else {
          sub_merge_index[sub_key] = builder.move_request.move_sub_entrys.size();
          builder.move_request.move_sub_entrys.push_back({source_entry, op_count});
        }
      }

      // 2. 目标Grid: Add-only — 构造临时 entry 作为数据模板
      {
        PROJECT_NAMESPACE_ID::DItemInstance move_in_instance = source_entry->item_instance;
        move_in_instance.mutable_item_basic()->set_count(op_count);
        if (!is_whole_move) {
          move_in_instance.mutable_item_basic()->set_guid(0);
        }
        *move_in_instance.mutable_item_basic()->mutable_position() = req.target_position;

        auto move_in_entry =
            ::excel::excel_config_type_traits::make_shared<ItemGridEntry>(std::move(move_in_instance));
        checked.constructed_entries.push_back(move_in_entry);

        auto& builder = builders[target_grid];
        builder.grid = target_grid;
        builder.original_indices.push_back(i);
        builder.move_request.move_add_entrys.push_back({move_in_entry, req.target_position, op_count});
      }
    }
  }

  // ============================================================
  // Phase 1: Grid 层 check_move — 验证位置可行性
  // ============================================================
  for (auto& pair : builders) {
    auto& builder = pair.second;
    auto grid_checked = builder.grid->check_move(config_group, builder.move_request);
    if (grid_checked.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      checked.error_code = grid_checked.error_code;
      if (!builder.original_indices.empty()) {
        checked.failed_index = static_cast<int32_t>(builder.original_indices[0]);
      }
      return checked;
    }
  }

  // ============================================================
  // Phase 2: 保存 per-grid 数据供 move() 使用
  // ============================================================
  checked.grid_data.reserve(builders.size());
  for (auto& pair : builders) {
    auto& builder = pair.second;
    ItemGridContainerMoveCheckedRequest::PerGridData gd;
    gd.grid = builder.grid;
    gd.move_request = std::move(builder.move_request);
    gd.original_indices = std::move(builder.original_indices);
    checked.grid_data.push_back(std::move(gd));
  }

  return checked;
}

// ============================================================
// Move — move: 基于 checked_request 执行实际移动
// ============================================================

ItemGridOperationResult ItemGridContainer::move(const ItemGridContainerMoveCheckedRequest& checked_request) {
  ItemGridOperationResult result;

  if (checked_request.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    result.error_code = checked_request.error_code;
    result.failed_index = checked_request.failed_index;
    return result;
  }

  for (const auto& gd : checked_request.grid_data) {
    // 构建 Grid 层 checked request (已验证, error_code 默认 EN_SUCCESS)
    ItemGridMoveCheckedRequest grid_checked{checked_request.config_group, gd.move_request};
    auto exec_result = gd.grid->move(grid_checked);
    if (exec_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = exec_result.error_code;
      if (!gd.original_indices.empty()) {
        result.failed_index = static_cast<int32_t>(gd.original_indices[0]);
      }
      return result;
    }
  }

  return result;
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
