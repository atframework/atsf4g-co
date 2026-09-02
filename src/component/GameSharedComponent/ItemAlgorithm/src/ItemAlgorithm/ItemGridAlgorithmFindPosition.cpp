// Copyright 2026 atframework

#include "ItemAlgorithm/ItemGridAlgorithmFindPosition.h"

#include "ItemAlgorithm/ItemGridAlgorithm.h"

namespace {

// 平台无关的 ctz (count trailing zeros), v 必须非 0
inline int ctz64(uint64_t v) {
#if defined(_MSC_VER)
  unsigned long index = 0;
  _BitScanForward64(&index, v);
  return static_cast<int>(index);
#else
  return __builtin_ctzll(v);
#endif
}

}  // namespace

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

namespace {

// 从 DItemBasic / DItemInstance 提取 DItemBasic 的访问器
//
// 仅提供只读 get_basic() 与可写 mutable_basic(), 具体操作 (设置 count / 位置 /
// 清空位置等) 由外层通过 mutable_basic() 完成。
template <typename ItemT>
struct ItemGridFindPositionAccessor;

template <>
struct ItemGridFindPositionAccessor<PROJECT_NAMESPACE_ID::DItemBasic> {
  using item_type = PROJECT_NAMESPACE_ID::DItemBasic;

  static const PROJECT_NAMESPACE_ID::DItemBasic& get_basic(const item_type& v) { return v; }
  static PROJECT_NAMESPACE_ID::DItemBasic* mutable_basic(item_type& v) { return &v; }
};

template <>
struct ItemGridFindPositionAccessor<PROJECT_NAMESPACE_ID::DItemInstance> {
  using item_type = PROJECT_NAMESPACE_ID::DItemInstance;

  static const PROJECT_NAMESPACE_ID::DItemBasic& get_basic(const item_type& v) { return v.item_basic(); }
  static PROJECT_NAMESPACE_ID::DItemBasic* mutable_basic(item_type& v) { return v.mutable_item_basic(); }
};

}  // namespace

// ============================================================
// 寻位辅助 — ignore_item 校验 + reserved 构建
// ============================================================

bool ItemGridAlgorithm::find_positions_validate_ignore(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
    std::unordered_map<int64_t, int64_t>& ignore_guid_count, std::unordered_map<int32_t, int64_t>& ignore_type_count,
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>& ignore_position_count)
    const {
  // 消耗的道具在找位置时要从可堆叠容量中扣除 (消耗后原位空出容量, 兑换回来的
  // 道具可以堆回原位)。按 GUID / 位置 / 类型三种方式匹配, 同一批次内累计。
  for (const auto& ignore : ignore_item) {
    if (!is_item_valid(config_group, ignore) || ignore.position().container_guid() != container_guid_ ||
        !check_item_position(ignore.position())) {
      ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: invalid ignore item type={} count={} guid={}",
                                     ignore.type_id(), ignore.count(), ignore.guid());
      return false;
    }

    auto* ignore_cfg = ItemAlgorithmTypeOption::GetItemType(ignore.type_id());
    if (ignore_cfg == nullptr) {
      ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: unknown ignore item type={}", ignore.type_id());
      return false;
    }

    if (ignore_cfg->need_guid) {
      auto guid_it = guid_index_.find(ignore.guid());
      if (guid_it == guid_index_.end() || guid_it->second->item_instance().item_basic().type_id() != ignore.type_id()) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore guid={} not found", ignore.guid());
        return false;
      }
      int64_t required = ignore_guid_count[ignore.guid()] + ignore.count();
      if (guid_it->second->item_instance().item_basic().count() < required) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore guid={} not enough", ignore.guid());
        return false;
      }
      ignore_guid_count[ignore.guid()] = required;
      // 记录该 GUID 条目所在位置, 供 reserved 释放 (整体消耗时允许新道具落回原位)
      if (ignore_cfg->need_occupy_the_grid) {
        ItemGridPosition pos =
            extract_position(guid_it->second->item_instance().item_basic().position().grid_position());
        ignore_position_count[pos] += ignore.count();
      }
    } else if (ignore_cfg->need_occupy_the_grid) {
      ItemGridPosition pos = extract_position(ignore.position().grid_position());
      auto pos_it = position_index_.find(pos);
      if (pos_it == position_index_.end() ||
          pos_it->second->item_instance().item_basic().type_id() != ignore.type_id()) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore position ({},{}) not found", pos.x,
                                       pos.y);
        return false;
      }
      int64_t required = ignore_position_count[pos] + ignore.count();
      if (pos_it->second->item_instance().item_basic().count() < required) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore position ({},{}) not enough", pos.x,
                                       pos.y);
        return false;
      }
      ignore_position_count[pos] = required;
    } else {
      auto group_it = item_groups_.find(ignore.type_id());
      if (group_it == item_groups_.end()) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore type={} group empty",
                                       ignore.type_id());
        return false;
      }
      int64_t total = 0;
      for (const auto& entry : group_it->second) {
        if (entry) {
          total += entry->item_instance().item_basic().count();
        }
      }
      int64_t required = ignore_type_count[ignore.type_id()] + ignore.count();
      if (total < required) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore type={} not enough", ignore.type_id());
        return false;
      }
      ignore_type_count[ignore.type_id()] = required;
    }
  }
  return true;
}

ItemGridOccupyFlag ItemGridAlgorithm::find_positions_build_reserved(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
        ignore_position_count) const {
  // 批次内格子预留副本 (care_item_size 模式)：记录已分配格子，避免批次内冲突，不修改实际背包数据
  ItemGridOccupyFlag reserved;
  if (is_occupy_flag()) {
    reserved = occupy_grid_flag_;

    // ignore_item 中被整体消耗的格子应从 reserved 释放 (先消耗再放入的真实语义)。
    // 这样带 GUID 道具 / 消耗位与新道具类型不同的场景也能落回原位。
    for (const auto& ignore_pair : ignore_position_count) {
      const ItemGridPosition& pos = ignore_pair.first;
      // 该位置被消耗的数量
      int64_t consumed = ignore_pair.second;
      auto pos_it = position_index_.find(pos);
      if (pos_it == position_index_.end()) {
        continue;
      }
      const auto& eb = pos_it->second->item_instance().item_basic();
      if (eb.count() <= consumed) {
        // 整体消耗: 释放该条目占用的区域
        auto* ignore_pos_cfg = get_item_position_cfg(config_group, eb);
        if (ignore_pos_cfg != nullptr) {
          int32_t item_row = ignore_pos_cfg->row_size();
          int32_t item_col = ignore_pos_cfg->column_size();
          if (item_row <= 0) {
            item_row = 1;
          }
          if (item_col <= 0) {
            item_col = 1;
          }
          for (int32_t r = 0; r < item_row; ++r) {
            for (int32_t c = 0; c < item_col; ++c) {
              int32_t rr = pos.y + r;
              int32_t cc = pos.x + c;
              if (rr >= 0 && rr < row_size_ && cc >= 0 && cc < column_size_) {
                reserved.set(cc, rr, false);
              }
            }
          }
        }
      }
    }
  }
  return reserved;
}

template <typename ItemT>
bool ItemGridAlgorithm::find_positions_inner(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const google::protobuf::RepeatedPtrField<ItemT>& items,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
    google::protobuf::RepeatedPtrField<ItemT>& success_item,
    google::protobuf::RepeatedPtrField<ItemT>& failed_item) const {
  using accessor = ItemGridFindPositionAccessor<ItemT>;

  if (!init_) {
    ITEM_ALGORITHM_LOG_ERROR_FMT("find_positions_for_basics called before init, container_guid={} operate_id={}",
                                 container_guid_, operate_id_);
    return false;
  }

  success_item.Clear();
  failed_item.Clear();
  success_item.Reserve(items.size());
  failed_item.Reserve(items.size());

  // -----------------------------------------------------------------------
  // 模式约束: 无限格子不占格模式与无位置模式不支持 ignore_item
  // -----------------------------------------------------------------------
  if (!ignore_item.empty() && !is_occupy_flag()) {
    ITEM_ALGORITHM_LOG_WARNING_FMT(
        "find_positions_for_basics failed: ignore_item is not supported in non-occupy mode, count={}",
        ignore_item.size());
    return false;
  }

  // ignore_item 存在性校验 + 消耗累计
  std::unordered_map<int64_t, int64_t> ignore_guid_count;
  std::unordered_map<int32_t, int64_t> ignore_type_count;
  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> ignore_position_count;
  if (!find_positions_validate_ignore(config_group, ignore_item, ignore_guid_count, ignore_type_count,
                                      ignore_position_count)) {
    return false;
  }

  // 批次内格子预留副本 (含 ignore_item 整体消耗格子的释放)
  ItemGridOccupyFlag reserved = find_positions_build_reserved(config_group, ignore_position_count);

  // -----------------------------------------------------------------------
  // 优化：按物品尺寸 (rows, cols) 记录线性扫描游标。
  //
  // key: (item_rows as uint32_t) << 32 | (item_cols as uint32_t)
  // value: (last_placed_y, last_placed_x)   ← 下次从此处开始
  // -----------------------------------------------------------------------
  std::unordered_map<uint64_t, std::pair<int32_t, int32_t>> size_scan_cursors;

  // 第一段: 策略1 (首选位置) + 策略2 (堆叠), 剩余物品记录到 pending_strategy3
  std::vector<ItemGridPendingFindPositionItem<ItemT>> pending_strategy3;
  if (!find_positions_stack_process(config_group, items, reserved, ignore_position_count, success_item, failed_item,
                                    pending_strategy3)) {
    return false;
  }

  // 第二段: 剩余物品按大小排序后, 策略3 位图扫描
  find_positions_occupy_process(config_group, reserved, success_item, failed_item, pending_strategy3);

  ITEM_ALGORITHM_LOG_DEBUG_FMT("find_positions_for_basics pass, {} positions found, {} failed", success_item.size(),
                               failed_item.size());
  return true;
}

template <typename ItemT>
bool ItemGridAlgorithm::find_positions_stack_process(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const google::protobuf::RepeatedPtrField<ItemT>& items, ItemGridOccupyFlag& reserved,
    const std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
        ignore_position_count,
    google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item,
    std::vector<ItemGridPendingFindPositionItem<ItemT>>& pending_strategy3) const {
  using accessor = ItemGridFindPositionAccessor<ItemT>;

  // 批次内已规划到已有条目的数量 (位置 → 本批次累计规划), 避免同一批多个可堆叠
  // basic 重复预订同一格容量导致超过 accumulation_limit。
  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_existing_extra;

  // 复用临时 DItemInstance（避免循环体内多次分配），用于构造 on_check_add 入参
  PROJECT_NAMESPACE_ID::DItemInstance tmp_inst;

  for (const auto& item : items) {
    const auto& basic = accessor::get_basic(item);
    auto* item_type_cfg = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
    if (!item_type_cfg) {
      ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: unknown item type={}", basic.type_id());
      return false;
    }

    // 无位置模式: 只检查能否放入 (道具是否不占格), 不寻找位置。
    // 占格道具无法放入, 整体失败 (外层负责按 Grid 分发, 属调用问题)。
    if (is_ignore_position()) {
      if (item_type_cfg->need_occupy_the_grid) {
        ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: ignore_position mode, type={}",
                                       basic.type_id());
        return false;
      }
      auto* out = success_item.Add();
      *out = item;
      accessor::mutable_basic(*out)->clear_position();
      continue;
    }

    // 非占格道具（货币/虚拟道具）：不需要格子位置，输出默认值
    if (!item_type_cfg->need_occupy_the_grid) {
      auto* out = success_item.Add();
      *out = item;
      accessor::mutable_basic(*out)->clear_position();
      continue;
    }

    // 获取物品尺寸配置
    auto* pos_cfg = get_item_position_cfg(config_group, basic);
    if (!pos_cfg) {
      ITEM_ALGORITHM_LOG_WARNING_FMT("find_positions_for_basics failed: item position cfg not found type={}",
                                     basic.type_id());
      return false;
    }
    const int32_t item_rows = is_occupy_flag() ? pos_cfg->row_size() : 1;
    const int32_t item_cols = is_occupy_flag() ? pos_cfg->column_size() : 1;
    // 与 check_add / check_move 一致: accumulation_limit <= 0 (配置缺省) 视为无限堆叠
    int64_t accumulation_limit = pos_cfg->accumulation_limit();
    if (accumulation_limit <= 0) {
      accumulation_limit = INT32_MAX;
    }

    // 辅助：构造含候选位置的临时请求并通过 on_check_add 做额外校验
    auto check_pos_ok = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos) -> bool {
      tmp_inst.Clear();
      *tmp_inst.mutable_item_basic() = basic;
      *tmp_inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = cand_pos;
      return on_check_add(config_group, tmp_inst) == PROJECT_NAMESPACE_ID::EN_SUCCESS;
    };

    // ---- 无限格子不占格模式（装备槽等）：完全委托给子类钩子，不做格子扫描 ----
    if (!is_occupy_flag()) {
      PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
      if (on_find_position_for_infinite(config_group, basic, out_pos) && check_pos_ok(out_pos)) {
        auto* out = success_item.Add();
        *out = item;
        auto* out_basic = accessor::mutable_basic(*out);
        out_basic->set_count(basic.count());
        *out_basic->mutable_position()->mutable_grid_position() = out_pos;
      } else {
        // 子类无法确定位置, 放入 failed_item, 由调用方决定后续处理
        ITEM_ALGORITHM_LOG_WARNING_FMT(
            "find_positions_for_basics: on_find_position_for_infinite rejected type={}, move to failed_item",
            basic.type_id());
        auto* out = failed_item.Add();
        *out = item;
        accessor::mutable_basic(*out)->set_count(basic.count());
      }
      continue;
    }

    // ---- care_item_size 模式：策略1 + 策略2 + on_check_add 校验 + 拆堆放置 ----

    // 辅助：检查 (x,y) 起点的 item_cols×item_rows 区域是否在 reserved 中全部空闲
    auto is_free_in_reserved = [&](int32_t x, int32_t y) -> bool {
      if (!is_item_in_range(x, y, item_rows, item_cols)) {
        return false;
      }
      for (int32_t r = 0; r < item_rows; ++r) {
        for (int32_t c = 0; c < item_cols; ++c) {
          if (reserved.is_occupied(x + c, y + r)) {
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
          reserved.set(x + c, y + r, true);
        }
      }
    };

    // 辅助：向 success_item 追加一个"放 count 到 cand_pos"的元素
    auto push_success = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos, int64_t count) {
      auto* out = success_item.Add();
      *out = item;
      auto* out_basic = accessor::mutable_basic(*out);
      out_basic->set_count(count);
      *out_basic->mutable_position()->mutable_grid_position() = cand_pos;
    };

    // 剩余待放置数量 (拆堆时逐步减少)
    int64_t remaining = basic.count();

    // 策略 1：若 basic 自带首选位置，优先尝试 (按容量吸收)
    if (remaining > 0) {
      ItemGridPosition preferred = extract_position(basic.position().grid_position());
      if (preferred.x >= 0 && preferred.y >= 0 && is_free_in_reserved(preferred.x, preferred.y)) {
        PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
        apply_position(out_pos, preferred);
        if (check_pos_ok(out_pos)) {
          int64_t put = std::min<int64_t>(remaining, accumulation_limit);
          push_success(out_pos, put);
          mark_reserved(preferred.x, preferred.y);
          // 记录本批次已规划到该位置的数量, 供策略2 计算剩余容量时扣除
          pending_existing_extra[preferred] += put;
          remaining -= put;
          ITEM_ALGORITHM_LOG_DEBUG_FMT("find_positions_for_basics: preferred type={} at ({},{}) put={}",
                                       basic.type_id(), preferred.x, preferred.y, put);
        }
      }
    }

    // 策略 2：堆叠到已有的同类型无GUID条目 (支持拆堆)
    // 注意: ignore_item 消耗的数量要从可堆叠容量中扣除 (消耗后原位空出容量)。
    // 即使堆叠上限为 1, 只要 ignore_item 消耗了该位置的数量 (空出容量), 也能堆回原位。
    // 同一批多个可堆叠 basic 规划到同一位置时, 需扣除本批次已规划数量 (pending_existing_extra)。
    if (remaining > 0 && basic.guid() == 0) {
      for (const auto& kv : position_index_) {
        if (remaining <= 0) {
          break;
        }
        const auto& eb = kv.second->item_instance().item_basic();
        if (eb.type_id() != basic.type_id() || eb.guid() != 0) {
          continue;
        }
        // 该位置本次消耗的数量 (消耗后空出容量)
        int64_t ignored = 0;
        auto ignore_it = ignore_position_count.find(kv.first);
        if (ignore_it != ignore_position_count.end()) {
          ignored = ignore_it->second;
        }
        // 本批次已规划到该位置的数量 (避免重复预订容量)
        int64_t already_planned = 0;
        auto planned_it = pending_existing_extra.find(kv.first);
        if (planned_it != pending_existing_extra.end()) {
          already_planned = planned_it->second;
        }
        int64_t remaining_capacity = accumulation_limit - eb.count() + ignored - already_planned;
        if (remaining_capacity <= 0) {
          continue;
        }
        int64_t put = std::min<int64_t>(remaining, remaining_capacity);
        PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
        apply_position(out_pos, kv.first);
        if (check_pos_ok(out_pos)) {
          push_success(out_pos, put);
          pending_existing_extra[kv.first] = already_planned + put;
          // 标记 reserved, 防止策略1/3 把该格当作空闲格重复预订
          // (普通已有条目的格子本为 true, 重复标记无副作用; 被 ignore_item 释放的格子需重新标记)
          mark_reserved(kv.first.x, kv.first.y);
          remaining -= put;
          ITEM_ALGORITHM_LOG_DEBUG_FMT("find_positions_for_basics: stack to existing type={} at ({},{}) put={}",
                                       basic.type_id(), kv.first.x, kv.first.y, put);
        }
      }
    }

    // 记录剩余待策略3 的物品
    if (remaining > 0) {
      ItemGridPendingFindPositionItem<ItemT> pending;
      pending.item = &item;
      pending.remaining = remaining;
      pending.item_rows = item_rows;
      pending.item_cols = item_cols;
      pending.accumulation_limit = accumulation_limit;
      pending_strategy3.push_back(pending);
    }
  }
  return true;
}

// ============================================================
// 寻位辅助 — 第二段 (策略3 位图扫描)
// ============================================================

template <typename ItemT>
void ItemGridAlgorithm::find_positions_occupy_process(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    ItemGridOccupyFlag& reserved, google::protobuf::RepeatedPtrField<ItemT>& success_item,
    google::protobuf::RepeatedPtrField<ItemT>& failed_item,
    std::vector<ItemGridPendingFindPositionItem<ItemT>>& pending_strategy3) const {
  using accessor = ItemGridFindPositionAccessor<ItemT>;

  // 剩余物品按大小从大到小排序后, 统一跑策略3 (位图扫描)
  std::stable_sort(
      pending_strategy3.begin(), pending_strategy3.end(),
      [](const ItemGridPendingFindPositionItem<ItemT>& lhs, const ItemGridPendingFindPositionItem<ItemT>& rhs) {
        return lhs.item_rows * lhs.item_cols > rhs.item_rows * rhs.item_cols;
      });

  // 按物品尺寸 (rows, cols) 记录线性扫描游标。
  // key: (item_rows as uint32_t) << 32 | (item_cols as uint32_t)
  // value: (last_placed_y, last_placed_x)   ← 下次从此处开始
  std::unordered_map<uint64_t, std::pair<int32_t, int32_t>> size_scan_cursors;

  // 复用临时 DItemInstance（避免循环体内多次分配），用于构造 on_check_add 入参
  PROJECT_NAMESPACE_ID::DItemInstance tmp_inst;

  for (const auto& pending : pending_strategy3) {
    const auto& item = *pending.item;
    const auto& basic = accessor::get_basic(item);
    const int32_t item_rows = pending.item_rows;
    const int32_t item_cols = pending.item_cols;
    const int64_t accumulation_limit = pending.accumulation_limit;
    int64_t remaining = pending.remaining;

    // 辅助：构造含候选位置的临时请求并通过 on_check_add 做额外校验
    auto check_pos_ok = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos) -> bool {
      tmp_inst.Clear();
      *tmp_inst.mutable_item_basic() = basic;
      *tmp_inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = cand_pos;
      return on_check_add(config_group, tmp_inst) == PROJECT_NAMESPACE_ID::EN_SUCCESS;
    };

    // 辅助：在 reserved 中标记 (x,y) 起点的区域为已占用
    auto mark_reserved = [&](int32_t x, int32_t y) {
      for (int32_t r = 0; r < item_rows; ++r) {
        for (int32_t c = 0; c < item_cols; ++c) {
          reserved.set(x + c, y + r, true);
        }
      }
    };

    // 辅助：向 success_item 追加一个"放 count 到 cand_pos"的元素
    auto push_success = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos, int64_t count) {
      auto* out = success_item.Add();
      *out = item;
      auto* out_basic = accessor::mutable_basic(*out);
      out_basic->set_count(count);
      *out_basic->mutable_position()->mutable_grid_position() = cand_pos;
    };

    // 策略 3：位图扫描背包寻找空闲格子 (游标推进 + 位运算找空位 + 跨段掩码检查)
    if (remaining > 0) {
      const uint64_t size_key = (static_cast<uint64_t>(static_cast<uint32_t>(item_rows)) << 32) |
                                static_cast<uint64_t>(static_cast<uint32_t>(item_cols));
      int32_t start_y = 0;
      int32_t start_x = 0;
      auto cursor_it = size_scan_cursors.find(size_key);
      if (cursor_it != size_scan_cursors.end()) {
        start_y = cursor_it->second.first;
        start_x = cursor_it->second.second;
      }

      const size_t words_per_row = reserved.words_per_row();
      const auto& data = reserved.data();
      const int32_t max_x = column_size_ - item_cols;
      const int32_t max_y = row_size_ - item_rows;

      // 检查 (x, y) 起点的 item_cols × item_rows 区域是否空闲 (位运算, 支持跨段)
      auto is_area_free_bitmap = [&](int32_t x, int32_t y) -> bool {
        if (!is_item_in_range(x, y, item_rows, item_cols)) {
          return false;
        }
        const size_t start_word = static_cast<size_t>(x) >> 6;
        const size_t start_bit = static_cast<size_t>(x) & 63;
        const size_t cols = static_cast<size_t>(item_cols);
        for (int32_t r = 0; r < item_rows; ++r) {
          const size_t row_base = static_cast<size_t>(y + r) * words_per_row;
          size_t remaining_bits = cols;
          size_t word_idx = start_word;
          size_t bit_offset = start_bit;
          while (remaining_bits > 0) {
            const size_t bits_in_word = std::min<size_t>(remaining_bits, 64 - bit_offset);
            const uint64_t mask = (bits_in_word == 64) ? ~uint64_t{0} : ((uint64_t{1} << bits_in_word) - 1);
            const uint64_t shifted_mask = mask << bit_offset;
            if (data[row_base + word_idx] & shifted_mask) {
              return false;
            }
            remaining_bits -= bits_in_word;
            ++word_idx;
            bit_offset = 0;
          }
        }
        return true;
      };

      // 从 (x, y) 起, 找该行第一个空闲起点 (>= x), 返回 x 或 -1
      auto find_next_free_x = [&](int32_t x, int32_t y) -> int32_t {
        const size_t row_base = static_cast<size_t>(y) * words_per_row;
        size_t word_idx = static_cast<size_t>(x) >> 6;
        size_t bit_offset = static_cast<size_t>(x) & 63;
        while (word_idx < words_per_row) {
          uint64_t free_mask = ~data[row_base + word_idx];
          if (bit_offset > 0) {
            free_mask &= ~((uint64_t{1} << bit_offset) - 1);
          }
          // 清除无效高位 (最后一 word 中超出 column_size_ 的列)
          const size_t word_col_start = word_idx * 64;
          const size_t valid_bits = (word_col_start + 64 <= static_cast<size_t>(column_size_))
                                        ? 64
                                        : (static_cast<size_t>(column_size_) - word_col_start);
          if (valid_bits < 64) {
            free_mask &= (valid_bits == 0) ? 0 : ((uint64_t{1} << valid_bits) - 1);
          }
          if (free_mask != 0) {
            const size_t first_bit = static_cast<size_t>(ctz64(free_mask));
            const int32_t candidate_x = static_cast<int32_t>(word_idx * 64 + first_bit);
            if (candidate_x <= max_x) {
              return candidate_x;
            }
          }
          ++word_idx;
          bit_offset = 0;
        }
        return -1;
      };

      for (int32_t y = start_y; y <= max_y && remaining > 0; ++y) {
        int32_t x = (y == start_y) ? start_x : 0;
        while (x <= max_x && remaining > 0) {
          // 用位运算找下一个空闲起点
          const int32_t next_x = find_next_free_x(x, y);
          if (next_x < 0 || next_x > max_x) {
            break;  // 该行无更多空闲起点
          }
          x = next_x;
          if (!is_area_free_bitmap(x, y)) {
            // 该起点放不下 (跨段或多行冲突), 从 x+1 继续
            ++x;
            continue;
          }
          ItemGridPosition pos{x, y};
          PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
          apply_position(out_pos, pos);
          if (!check_pos_ok(out_pos)) {
            ++x;
            continue;
          }
          int64_t put = std::min<int64_t>(remaining, accumulation_limit);
          push_success(out_pos, put);
          mark_reserved(x, y);
          remaining -= put;
          // 更新游标：下次同尺寸物品从此位置继续（该位置已标记，扫描会自然跳过）
          size_scan_cursors[size_key] = {y, x};
          ITEM_ALGORITHM_LOG_DEBUG_FMT("find_positions_for_basics: scan type={} at ({},{}) put={}", basic.type_id(), x,
                                       y, put);
          // 放置后 x 前进 item_cols (该区域已标记)
          x += item_cols;
        }
      }
    }

    if (remaining > 0) {
      // 背包放不下剩余部分, 放入 failed_item, 由调用方决定后续处理 (如传入下一个背包)
      ITEM_ALGORITHM_LOG_WARNING_FMT(
          "find_positions_for_basics: no free position for type={} count={}, remaining={} move to failed_item",
          basic.type_id(), basic.count(), remaining);
      auto* out = failed_item.Add();
      *out = item;
      accessor::mutable_basic(*out)->set_count(remaining);
    }
  }
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END