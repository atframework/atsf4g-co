// Copyright 2026 atframework

#include "ItemAlgorithm/ItemGridAlgorithmFindPosition.h"

#include "ItemAlgorithm/ItemGridAlgorithm.h"

#include "config/excel/item_type_config.h"

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

#define ITEM_ALGORITHM_LOG_HELPER(level, message) algorithm->log((level), __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_HELPER_FMT(level, ...) \
  algorithm->log((level), __FILE__, __LINE__, LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_HELPER_DEBUG(message) \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kDebug, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_HELPER_INFO(message) \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kInfo, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_HELPER_WARNING(message) \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kWarning, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_HELPER_ERROR(message) \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kError, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_HELPER_DEBUG_FMT(...)                                                          \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kDebug, __FILE__, __LINE__, \
                 LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_HELPER_INFO_FMT(...)                                                          \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kInfo, __FILE__, __LINE__, \
                 LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT(...)                                                          \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kWarning, __FILE__, __LINE__, \
                 LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_HELPER_ERROR_FMT(...)                                                          \
  algorithm->log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kError, __FILE__, __LINE__, \
                 LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))

class ItemGridAlgorithmFindPositionHelper {
 public:
  // 寻位公共实现 (模板, 支持 DItemBasic / DItemInstance 两种输入)
  template <typename ItemT>
  static bool find_positions_inner(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridAlgorithm* algorithm, const google::protobuf::RepeatedPtrField<ItemT>& items,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
      google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item);

  // 寻位辅助: ignore_item 存在性校验 + 消耗累计 (按 GUID / 位置 / 类型)
  static bool find_positions_validate_ignore(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridAlgorithm* algorithm,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
      std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
          ignore_position_count);

  // 寻位辅助: 构建批次内格子预留副本 reserved (含 ignore_item 整体消耗格子的释放)
  static ItemGridOccupyFlag find_positions_build_reserved(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridAlgorithm* algorithm,
      const std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
          ignore_position_count);

  template <typename ItemT>
  static bool find_positions_stack_process(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridAlgorithm* algorithm, const google::protobuf::RepeatedPtrField<ItemT>& items,
      ItemGridOccupyFlag& reserved,
      const std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
          ignore_position_count,
      google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item,
      std::vector<ItemGridPendingFindPositionItem<ItemT>>& pending_find_position);

  template <typename ItemT>
  static void find_positions_occupy_process(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridAlgorithm* algorithm, ItemGridOccupyFlag& reserved,
      google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item,
      std::vector<ItemGridPendingFindPositionItem<ItemT>>& pending_find_position);
};

bool ItemGridAlgorithmFindPositionHelper::find_positions_validate_ignore(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const ItemGridAlgorithm* algorithm,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
    std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
        ignore_position_count) {
  std::unordered_set<int64_t> ignore_guid;
  std::unordered_map<int32_t, int64_t> ignore_type_count;

  // 消耗的道具在找位置时要从可堆叠容量中扣除 (消耗后原位空出容量, 兑换回来的
  // 道具可以堆回原位)。按 GUID / 位置 / 类型三种方式匹配, 同一批次内累计。
  for (const auto& ignore : ignore_item) {
    if (!algorithm->is_item_valid(config_group, ignore) ||
        ignore.position().container_guid() != algorithm->container_guid_ ||
        !algorithm->check_item_position(ignore.position())) {
      ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT(
          "find_positions_for_basics failed: invalid ignore item type={} count={} guid={}", ignore.type_id(),
          ignore.count(), ignore.guid());
      return false;
    }

    auto* ignore_cfg = ItemAlgorithmTypeOption::GetItemType(ignore.type_id());
    if (ignore_cfg == nullptr) {
      ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: unknown ignore item type={}",
                                            ignore.type_id());
      return false;
    }

    if (ignore_cfg->need_guid) {
      auto guid_it = algorithm->guid_index_.find(ignore.guid());
      if (guid_it == algorithm->guid_index_.end() ||
          guid_it->second->item_instance().item_basic().type_id() != ignore.type_id()) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore guid={} not found",
                                              ignore.guid());
        return false;
      }
      if (ignore_guid.find(ignore.guid()) != ignore_guid.end()) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore guid={} duplicated",
                                              ignore.guid());
        return false;
      }
      if (guid_it->second->item_instance().item_basic().count() < ignore.count()) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore guid={} not enough",
                                              ignore.guid());
        return false;
      }
      ignore_guid.insert(ignore.guid());
      // 记录该 GUID 条目所在位置, 供 reserved 释放 (整体消耗时允许新道具落回原位)
      if (ignore_cfg->need_occupy_the_grid) {
        ItemGridPosition pos =
            algorithm->extract_position(guid_it->second->item_instance().item_basic().position().grid_position());
        ignore_position_count[pos] += ignore.count();
      }
    } else if (ignore_cfg->need_occupy_the_grid) {
      ItemGridPosition pos = algorithm->extract_position(ignore.position().grid_position());
      auto pos_it = algorithm->position_index_.find(pos);
      if (pos_it == algorithm->position_index_.end() ||
          pos_it->second->item_instance().item_basic().type_id() != ignore.type_id()) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore position ({},{}) not found",
                                              pos.x, pos.y);
        return false;
      }
      int64_t required = ignore_position_count[pos] + ignore.count();
      if (pos_it->second->item_instance().item_basic().count() < required) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore position ({},{}) not enough",
                                              pos.x, pos.y);
        return false;
      }
      ignore_position_count[pos] = required;
    } else {
      auto group_it = algorithm->item_groups_.find(ignore.type_id());
      if (group_it == algorithm->item_groups_.end()) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore type={} group empty",
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
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: ignore type={} not enough",
                                              ignore.type_id());
        return false;
      }
      ignore_type_count[ignore.type_id()] = required;
    }
  }
  return true;
}

ItemGridOccupyFlag ItemGridAlgorithmFindPositionHelper::find_positions_build_reserved(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const ItemGridAlgorithm* algorithm,
    const std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
        ignore_position_count) {
  // 批次内格子预留副本 (care_item_size 模式)：记录已分配格子，避免批次内冲突，不修改实际背包数据
  ItemGridOccupyFlag reserved;
  if (algorithm->is_occupy_flag()) {
    reserved = algorithm->occupy_grid_flag_;

    // ignore_item 中被整体消耗的格子应从 reserved 释放 (先消耗再放入的真实语义)。
    // 这样带 GUID 道具 / 消耗位与新道具类型不同的场景也能落回原位。
    for (const auto& ignore_pair : ignore_position_count) {
      const ItemGridPosition& pos = ignore_pair.first;
      // 该位置被消耗的数量
      int64_t consumed = ignore_pair.second;
      auto pos_it = algorithm->position_index_.find(pos);
      if (pos_it == algorithm->position_index_.end()) {
        continue;
      }
      const auto& eb = pos_it->second->item_instance().item_basic();
      if (eb.count() <= consumed) {
        // 整体消耗: 释放该条目占用的区域
        auto* ignore_pos_cfg = algorithm->get_item_position_cfg(config_group, eb);
        if (ignore_pos_cfg != nullptr) {
          int32_t item_row = ignore_pos_cfg->row_size();
          int32_t item_col = ignore_pos_cfg->column_size();
          for (int32_t r = 0; r < item_row; ++r) {
            for (int32_t c = 0; c < item_col; ++c) {
              int32_t rr = pos.y + r;
              int32_t cc = pos.x + c;
              if (rr >= 0 && rr < algorithm->row_size_ && cc >= 0 && cc < algorithm->column_size_) {
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
bool ItemGridAlgorithmFindPositionHelper::find_positions_stack_process(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const ItemGridAlgorithm* algorithm, const google::protobuf::RepeatedPtrField<ItemT>& items,
    ItemGridOccupyFlag& reserved,
    const std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo>&
        ignore_position_count,
    google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item,
    std::vector<ItemGridPendingFindPositionItem<ItemT>>& pending_find_position) {
  using accessor = ItemGridFindPositionAccessor<ItemT>;

  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> pending_existing_extra;

  for (const auto& item : items) {
    const auto& basic = accessor::get_basic(item);
    auto* item_type_cfg = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
    if (!item_type_cfg) {
      ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: unknown item type={}", basic.type_id());
      return false;
    }

    if (!algorithm->is_item_valid(config_group, basic)) {
      ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: invalid item type={} count={} guid={}",
                                            basic.type_id(), basic.count(), basic.guid());
      return false;
    }

    // 无位置模式
    if (algorithm->is_ignore_position()) {
      auto* out = success_item.Add();
      *out = item;
      algorithm->apply_position(*accessor::mutable_basic(*out)->mutable_position()->mutable_grid_position(),
                                ItemGridPosition{});
      accessor::mutable_basic(*out)->mutable_position()->set_container_guid(algorithm->container_guid_);
      continue;
    }

    // 获取物品尺寸配置
    auto* pos_cfg = algorithm->get_item_position_cfg(config_group, basic);
    if (!pos_cfg) {
      ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT("find_positions_for_basics failed: item position cfg not found type={}",
                                            basic.type_id());
      return false;
    }
    int32_t item_rows = 0;
    int32_t item_cols = 0;
    if (algorithm->is_occupy_flag()) {
      item_rows = pos_cfg->row_size();
      item_cols = pos_cfg->column_size();
      if (item_rows <= 0 || item_cols <= 0) {
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT(
            "find_positions_for_basics failed: invalid item size row={} col={} type={}", item_rows, item_cols,
            basic.type_id());
        return false;
      }
    }
    // 与 check_add / check_move 一致: accumulation_limit <= 0 (配置缺省) 视为无限堆叠
    int64_t accumulation_limit = pos_cfg->accumulation_limit();
    if (accumulation_limit <= 0) {
      accumulation_limit = INT32_MAX;
    }

    // 辅助：构造含候选位置的临时请求并通过 on_check_add 做额外校验
    auto check_pos_ok = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos) -> bool {
      PROJECT_NAMESPACE_ID::DItemInstance tmp_inst;
      tmp_inst.Clear();
      *tmp_inst.mutable_item_basic() = basic;
      *tmp_inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = cand_pos;
      return algorithm->on_check_add(config_group, tmp_inst) == PROJECT_NAMESPACE_ID::EN_SUCCESS;
    };

    // ---- 无限格子不占格模式（装备槽等）：完全委托给子类钩子，不做格子扫描 ----
    if (!algorithm->is_occupy_flag()) {
      PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
      if (algorithm->on_find_position_for_infinite(config_group, basic, out_pos) && check_pos_ok(out_pos)) {
        auto* out = success_item.Add();
        *out = item;
        auto* out_basic = accessor::mutable_basic(*out);
        out_basic->set_count(basic.count());
        *out_basic->mutable_position()->mutable_grid_position() = out_pos;
      } else {
        // 子类无法确定位置, 放入 failed_item, 由调用方决定后续处理
        ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT(
            "find_positions_for_basics: on_find_position_for_infinite rejected type={}, move to failed_item",
            basic.type_id());
        auto* out = failed_item.Add();
        *out = item;
        accessor::mutable_basic(*out)->set_count(basic.count());
      }
      continue;
    }

    // 剩余待放置数量 (拆堆时逐步减少)
    int64_t remaining = basic.count();

    // 堆叠到已有的同类型条目
    if (remaining > 0 && basic.guid() == 0) {
      auto group_it = algorithm->item_groups_.find(basic.type_id());
      if (group_it != algorithm->item_groups_.end()) {
        for (const auto& entry : group_it->second) {
          if (remaining <= 0) {
            break;
          }
          const auto& eb = entry->item_instance().item_basic();
          ItemGridPosition pos = algorithm->extract_position(eb.position().grid_position());
          int64_t ignored = 0;
          auto ignore_it = ignore_position_count.find(pos);
          if (ignore_it != ignore_position_count.end()) {
            ignored = ignore_it->second;
          }
          // 本批次已规划到该位置的数量 (避免重复预订容量)
          int64_t already_planned = 0;
          auto planned_it = pending_existing_extra.find(pos);
          if (planned_it != pending_existing_extra.end()) {
            already_planned = planned_it->second;
          }
          int64_t remaining_capacity = accumulation_limit - eb.count() + ignored - already_planned;
          if (remaining_capacity <= 0) {
            continue;
          }
          int64_t put = std::min<int64_t>(remaining, remaining_capacity);
          PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
          algorithm->apply_position(out_pos, pos);
          if (check_pos_ok(out_pos)) {
            {
              auto* out = success_item.Add();
              *out = item;
              auto* out_basic = accessor::mutable_basic(*out);
              out_basic->set_count(put);
              *out_basic->mutable_position()->mutable_grid_position() = out_pos;
              out_basic->mutable_position()->set_container_guid(algorithm->container_guid_);
            }
            pending_existing_extra[pos] = already_planned + put;
            // 标记 reserved, 把该格当作空闲格重复预订
            // (普通已有条目的格子本为 true, 重复标记无副作用; 被 ignore_item 释放的格子需重新标记)
            for (int32_t r = 0; r < item_rows; ++r) {
              for (int32_t c = 0; c < item_cols; ++c) {
                reserved.set(pos.x + c, pos.y + r, true);
              }
            }
            remaining -= put;
            ITEM_ALGORITHM_LOG_HELPER_DEBUG_FMT(
                "find_positions_for_basics: stack to existing type={} at ({},{}) put={}", basic.type_id(), pos.x, pos.y,
                put);
          }
        }
      }
    }

    // 记录剩余待寻找 的物品
    if (remaining > 0) {
      ItemGridPendingFindPositionItem<ItemT> pending;
      pending.item = &item;
      pending.remaining = remaining;
      pending.item_rows = item_rows;
      pending.item_cols = item_cols;
      pending.accumulation_limit = accumulation_limit;
      pending_find_position.push_back(pending);
    }
  }
  return true;
}

template <typename ItemT>
void ItemGridAlgorithmFindPositionHelper::find_positions_occupy_process(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const ItemGridAlgorithm* algorithm, ItemGridOccupyFlag& reserved,
    google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item,
    std::vector<ItemGridPendingFindPositionItem<ItemT>>& pending_find_position) {
  using accessor = ItemGridFindPositionAccessor<ItemT>;

  // 剩余物品按大小从大到小排序后, 统一跑位图扫描
  std::stable_sort(
      pending_find_position.begin(), pending_find_position.end(),
      [](const ItemGridPendingFindPositionItem<ItemT>& lhs, const ItemGridPendingFindPositionItem<ItemT>& rhs) {
        if (lhs.item_cols > rhs.item_cols) {
          return true;
        }
        return lhs.item_rows > rhs.item_rows;
      });

  // 记录线性扫描游标。
  uint64_t last_check_item_size_key = 0;
  int32_t start_x = 0;
  int32_t start_y = 0;

  for (const auto& pending : pending_find_position) {
    const auto& item = *pending.item;
    const auto& basic = accessor::get_basic(item);
    const int32_t item_rows = pending.item_rows;
    const int32_t item_cols = pending.item_cols;
    const int64_t accumulation_limit = pending.accumulation_limit;
    int64_t remaining = pending.remaining;

    // 辅助：构造含候选位置的临时请求并通过 on_check_add 做额外校验
    auto check_pos_ok = [&](const PROJECT_NAMESPACE_ID::DItemGridPosition& cand_pos) -> bool {
      PROJECT_NAMESPACE_ID::DItemInstance tmp_inst;
      tmp_inst.Clear();
      *tmp_inst.mutable_item_basic() = basic;
      *tmp_inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = cand_pos;
      return algorithm->on_check_add(config_group, tmp_inst) == PROJECT_NAMESPACE_ID::EN_SUCCESS;
    };

    // 位图扫描背包寻找空闲格子
    if (remaining > 0) {
      {
        const uint64_t size_key = (static_cast<uint64_t>(static_cast<uint32_t>(item_rows)) << 32) |
                                  static_cast<uint64_t>(static_cast<uint32_t>(item_cols));
        if (size_key != last_check_item_size_key) {
          start_x = 0;
          start_y = 0;
          last_check_item_size_key = size_key;
        }
      }
      const size_t words_per_row = reserved.words_per_row();
      const auto& data = reserved.data();
      const int32_t max_x = algorithm->column_size_ - item_cols;
      const int32_t max_y = algorithm->row_size_ - item_rows;

      // 检查 (x, y) 起点的 item_cols × item_rows 区域是否空闲 (位运算, 支持跨段)
      auto is_area_free_bitmap = [&](int32_t x, int32_t y) -> bool {
        if (!algorithm->is_item_in_range(x, y, item_rows, item_cols)) {
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
          const size_t valid_bits = (word_col_start + 64 <= static_cast<size_t>(algorithm->column_size_))
                                        ? 64
                                        : (static_cast<size_t>(algorithm->column_size_) - word_col_start);
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
          algorithm->apply_position(out_pos, pos);
          if (!check_pos_ok(out_pos)) {
            ++x;
            continue;
          }
          int64_t put = std::min<int64_t>(remaining, accumulation_limit);
          {
            auto* out = success_item.Add();
            *out = item;
            auto* out_basic = accessor::mutable_basic(*out);
            out_basic->set_count(put);
            *out_basic->mutable_position()->mutable_grid_position() = out_pos;
            out_basic->mutable_position()->set_container_guid(algorithm->container_guid_);
          }
          for (int32_t r = 0; r < item_rows; ++r) {
            for (int32_t c = 0; c < item_cols; ++c) {
              reserved.set(x + c, y + r, true);
            }
          }
          remaining -= put;
          ITEM_ALGORITHM_LOG_HELPER_DEBUG_FMT("find_positions_for_basics: scan type={} at ({},{}) put={}",
                                              basic.type_id(), x, y, put);
          // 放置后 x 前进 item_cols (该区域已标记)
          x += item_cols;
          // 更新游标
          start_x = x;
          start_y = y;
        }
      }
    }

    if (remaining > 0) {
      // 背包放不下剩余部分, 放入 failed_item, 由调用方决定后续处理 (如传入下一个背包)
      ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT(
          "find_positions_for_basics: no free position for type={} count={}, remaining={} move to failed_item",
          basic.type_id(), basic.count(), remaining);
      auto* out = failed_item.Add();
      *out = item;
      accessor::mutable_basic(*out)->set_count(remaining);
    }
  }
}

template <typename ItemT>
bool ItemGridAlgorithmFindPositionHelper::find_positions_inner(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const ItemGridAlgorithm* algorithm, const google::protobuf::RepeatedPtrField<ItemT>& items,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
    google::protobuf::RepeatedPtrField<ItemT>& success_item, google::protobuf::RepeatedPtrField<ItemT>& failed_item) {
  using accessor = ItemGridFindPositionAccessor<ItemT>;

  if (!algorithm->init_) {
    ITEM_ALGORITHM_LOG_HELPER_ERROR_FMT("find_positions_for_basics called before init, container_guid={} operate_id={}",
                                        algorithm->container_guid_, algorithm->operate_id_);
    return false;
  }

  success_item.Clear();
  failed_item.Clear();
  success_item.Reserve(items.size());
  failed_item.Reserve(items.size());

  // -----------------------------------------------------------------------
  // 模式约束: 无限格子不占格模式与无位置模式不支持 ignore_item
  // -----------------------------------------------------------------------
  if (!ignore_item.empty() && !algorithm->is_occupy_flag()) {
    ITEM_ALGORITHM_LOG_HELPER_WARNING_FMT(
        "find_positions_for_basics failed: ignore_item is not supported in non-occupy mode, count={}",
        ignore_item.size());
    return false;
  }

  // ignore_item 存在性校验 + 消耗累计
  std::unordered_map<ItemGridPosition, int64_t, ItemGridPositionHash, ItemGridPositionEqualTo> ignore_position_count;
  if (!find_positions_validate_ignore(config_group, algorithm, ignore_item, ignore_position_count)) {
    return false;
  }

  // 批次内格子预留副本 (含 ignore_item 整体消耗格子的释放)
  ItemGridOccupyFlag reserved = find_positions_build_reserved(config_group, algorithm, ignore_position_count);

  // 第一段: 尝试堆叠, 剩余物品记录到 pending_find_position
  std::vector<ItemGridPendingFindPositionItem<ItemT>> pending_find_position;
  if (!find_positions_stack_process(config_group, algorithm, items, reserved, ignore_position_count, success_item,
                                    failed_item, pending_find_position)) {
    return false;
  }

  if (algorithm->is_occupy_flag()) {
    // 第二段: 剩余物品按大小排序后, 位图扫描
    find_positions_occupy_process(config_group, algorithm, reserved, success_item, failed_item, pending_find_position);
  }

  ITEM_ALGORITHM_LOG_HELPER_DEBUG_FMT("find_positions_for_basics pass, {} positions found, {} failed",
                                      success_item.size(), failed_item.size());
  return true;
}

ITEM_ALGORITHM_API bool ItemGridAlgorithm::find_positions_for_basics(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed_item) const {
  if (&basics == &success_item || &basics == &failed_item || &success_item == &failed_item) {
    return false;
  }
  return ItemGridAlgorithmFindPositionHelper::find_positions_inner(config_group, this, basics, ignore_item,
                                                                   success_item, failed_item);
}

ITEM_ALGORITHM_API bool ItemGridAlgorithm::find_positions_for_instances(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& items,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& ignore_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const {
  if (&items == &success_item || &items == &failed_item || &success_item == &failed_item) {
    return false;
  }
  return ItemGridAlgorithmFindPositionHelper::find_positions_inner(config_group, this, items, ignore_item, success_item,
                                                                   failed_item);
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END