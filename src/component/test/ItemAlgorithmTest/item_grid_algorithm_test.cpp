// Copyright 2025 atframework

#include "frame/test_macros.h"

#include <ItemAlgorithm/ItemGridAlgorithm.h>
#include <ItemAlgorithm/ItemGridContainer.h>
#include <ItemAlgorithm/ItemGridData.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================
// 辅助常量 — 基于 EnItemType 范围定义道具 ID
// ============================================================

// EN_ITEM_TYPE_EQUIPMENT: [400000, 500000) — 占格, need_guid=true, 每件独立
static constexpr int32_t kEquipmentTypeId = 400001;

// EN_ITEM_TYPE_COIN: [1000, 10000)  — 不占格, 不需要GUID
static constexpr int32_t kCoinTypeId = 1001;

// EN_ITEM_TYPE_VIRTUAL: [10000, 100000) — 不占格, 不需要GUID
static constexpr int32_t kVirtualTypeId = 10001;

// EN_ITEM_TYPE_ITEM: [100000, 400000) — 占格, 默认不需要GUID (由 proto 配置 need_guid=false)
static constexpr int32_t kItemTypeId_1x1 = 100001;  // 1x1 大小, accumulation_limit = 99
static constexpr int32_t kItemTypeId_2x2 = 100002;  // 2x2 大小, accumulation_limit = 1
static constexpr int32_t kItemTypeId_2x1 = 100003;  // 2x1 大小 (宽2 高1), accumulation_limit = 1

static constexpr int32_t kHookRejectedError = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;

// ============================================================
// 测试用 ItemGridAlgorithm — Hook get_item_position_cfg
// ============================================================

ITEM_ALGORITHM_NAMESPACE_BEGIN
namespace item_algorithm {

class TestItemGridAlgorithm : public ItemGridAlgorithm {
 public:
  virtual ~TestItemGridAlgorithm() = default;
  /// @brief 注册 type_id → DItemPositionCfg 的映射 (替代配置表查询)
  void register_position_cfg(int32_t type_id, int32_t accumulation_limit, int32_t row_size, int32_t col_size) {
    auto& cfg = position_cfg_map_[type_id];
    cfg.set_accumulation_limit(accumulation_limit);
    cfg.set_row_size(row_size);
    cfg.set_column_size(col_size);
  }

 protected:
  /// @brief 创建同类型同配置的空 Grid, 并复制 position_cfg_map_ (供 check_replace 复用 check_add)
  item_algorithm::item_grid_algorithm_ptr_t create_empty_clone() const override {
    auto grid = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    copy_empty_config_to(*grid);
    grid->position_cfg_map_ = position_cfg_map_;
    return grid;
  }

  const PROJECT_NAMESPACE_ID::DItemPositionCfg* get_item_position_cfg(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
      const PROJECT_NAMESPACE_ID::DItemBasic& basic) const override {
    auto it = position_cfg_map_.find(basic.type_id());
    if (it != position_cfg_map_.end()) {
      return &it->second;
    }
    return nullptr;
  }

 private:
  std::unordered_map<int32_t, PROJECT_NAMESPACE_ID::DItemPositionCfg> position_cfg_map_;
};

struct ItemGridHookTestState {
  bool reject_all_add = false;
  bool reject_sub = false;
  bool reject_count_limit = false;
  int32_t rejected_position_x = -1;
  int32_t rejected_add_position_x = -1;
  int32_t check_item_position_calls = 0;
  int32_t on_check_add_calls = 0;
  int32_t on_check_sub_calls = 0;
  int32_t on_check_item_count_limit_calls = 0;

  void reset_call_counts() {
    check_item_position_calls = 0;
    on_check_add_calls = 0;
    on_check_sub_calls = 0;
    on_check_item_count_limit_calls = 0;
  }
};

class HookTestItemGridAlgorithm : public TestItemGridAlgorithm {
 public:
  HookTestItemGridAlgorithm() : state_(std::make_shared<ItemGridHookTestState>()) {}
  explicit HookTestItemGridAlgorithm(std::shared_ptr<ItemGridHookTestState> state) : state_(std::move(state)) {}

  ItemGridHookTestState& hook_state() { return *state_; }

 protected:
  item_algorithm::item_grid_algorithm_ptr_t create_empty_clone() const override {
    auto grid = atfw::util::memory::make_strong_rc<HookTestItemGridAlgorithm>(state_);
    copy_empty_config_to(*grid);
    grid->register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    grid->register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
    return grid;
  }

  int32_t on_check_add(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                       const PROJECT_NAMESPACE_ID::DItemInstance& request) const override {
    ++state_->on_check_add_calls;
    if (state_->reject_all_add ||
        (state_->rejected_add_position_x >= 0 &&
         get_inventory_x(request.item_basic().position()) == state_->rejected_add_position_x)) {
      return kHookRejectedError;
    }
    return TestItemGridAlgorithm::on_check_add(config_group, request);
  }

  int32_t on_check_sub(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                       const PROJECT_NAMESPACE_ID::DItemBasic& request) const override {
    ++state_->on_check_sub_calls;
    if (state_->reject_sub) {
      return kHookRejectedError;
    }
    return TestItemGridAlgorithm::on_check_sub(config_group, request);
  }

  int32_t on_check_item_count_limit(int32_t type_id, int64_t current_count, int64_t add_count) const override {
    ++state_->on_check_item_count_limit_calls;
    if (state_->reject_count_limit) {
      return kHookRejectedError;
    }
    return TestItemGridAlgorithm::on_check_item_count_limit(type_id, current_count, add_count);
  }

  bool check_item_position(const PROJECT_NAMESPACE_ID::DItemPosition& position) const override {
    ++state_->check_item_position_calls;
    return state_->rejected_position_x < 0 || get_inventory_x(position) != state_->rejected_position_x;
  }

 private:
  static int32_t get_inventory_x(const PROJECT_NAMESPACE_ID::DItemPosition& position) {
    if (position.grid_position().has_user_inventory()) {
      return position.grid_position().user_inventory().x();
    }
    return -1;
  }

 private:
  std::shared_ptr<ItemGridHookTestState> state_;
};

/// @brief 服务器端测试子类 — 在 on_item_data_changed 中只记录 entry_id
///
/// 用于模拟服务器操作 → 收集变更 → 同步到客户端子类 的完整流程。
/// collect_apply_data() 提取时实时通过 find_entry_by_id() 获取 Entry,
/// 从而保证同步的是最新数据 (如 mutable_item_data 的修改)。
class ServerTestItemGridAlgorithm : public TestItemGridAlgorithm {
 public:
  /// @brief 收集到的变更 Entry ID 集合 (仅记录 ID, 不缓存快照)
  const std::unordered_set<uint64_t>& get_entry_cache() const { return entry_cache_; }

  /// @brief 清空收集缓存 (每轮测试结束后可复用)
  void clear_change_cache() { entry_cache_.clear(); }

  /// @brief 将收集的 entry_id 转换为 apply_entries 接收的 protobuf 参数
  ///
  /// 实时通过 find_entry_by_id() 获取 Entry:
  ///   - entry 不存在 或 count <= 0 → 视为已删除, 放入 remove_entry_ids
  ///   - entry 存在且 count > 0   → 视为新增/更新, 放入 update_entries (DItemInstanceEntry)
  void collect_apply_data(
      ::google::protobuf::RepeatedField<uint64_t>& out_remove_ids,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry>& out_updates) const {
    out_remove_ids.Clear();
    out_updates.Clear();
    for (uint64_t entry_id : entry_cache_) {
      auto entry = find_entry_by_id(entry_id);
      if (!entry || entry->item_instance().item_basic().count() <= 0) {
        out_remove_ids.Add(entry_id);
      } else {
        auto* out_entry = out_updates.Add();
        out_entry->set_entry_id(entry_id);
        *out_entry->mutable_instance() = entry->item_instance();
      }
    }
  }

 protected:
  void on_item_data_changed(const item_grid_entry_ptr_t& entry, ItemGridOperationReason /*reason*/) override {
    if (entry) {
      entry_cache_.insert(entry->entry_id());
    }
  }

 private:
  std::unordered_set<uint64_t> entry_cache_;
};

}  // namespace item_algorithm
ITEM_ALGORITHM_NAMESPACE_END

#ifdef _WIN32
// Ensure Windows console uses UTF-8 so test log messages display Chinese correctly
namespace {
struct _ConsoleUtf8Initializer {
  _ConsoleUtf8Initializer() {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    // 启用 ANSI 转义序列 (用于 Error/Warning 着色输出)
    ::HANDLE std_out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    ::DWORD console_mode = 0;
    if (INVALID_HANDLE_VALUE != std_out && ::GetConsoleMode(std_out, &console_mode)) {
      ::SetConsoleMode(std_out, console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }
};
static _ConsoleUtf8Initializer _consoleUtf8Init;
}  // namespace
#endif

using namespace ITEM_ALGORITHM_NAMESPACE_ID;                  // ItemGridAddRequest, ItemGridSubRequest, etc.
using namespace ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm;  // ItemGridAlgorithm, ItemGridContainer, etc.

// ============================================================
// 辅助函数
// ============================================================

/// @brief 构造一个空的 config_group (测试中不使用, 因为 get_item_position_cfg 被 hook)
static ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> make_test_config_group() {
  return ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();
}

/// @brief 创建一个 DItemInstance (占格道具)
static PROJECT_NAMESPACE_ID::DItemInstance make_grid_item(int32_t type_id, int64_t count, int32_t x, int32_t y,
                                                          int64_t guid = 0) {
  PROJECT_NAMESPACE_ID::DItemInstance instance;
  auto* basic = instance.mutable_item_basic();
  basic->set_type_id(type_id);
  basic->set_count(count);
  basic->set_guid(guid);
  basic->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
  basic->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
  return instance;
}

/// @brief 创建一个 DItemInstance (不占格道具, 如货币/虚拟道具)
static PROJECT_NAMESPACE_ID::DItemInstance make_ungrid_item(int32_t type_id, int64_t count) {
  PROJECT_NAMESPACE_ID::DItemInstance instance;
  auto* basic = instance.mutable_item_basic();
  basic->set_type_id(type_id);
  basic->set_count(count);
  basic->set_guid(0);
  return instance;
}

/// @brief 创建一个 DItemBasic (用于 Sub 请求)
static PROJECT_NAMESPACE_ID::DItemBasic make_sub_basic(int32_t type_id, int64_t count, int32_t x = 0, int32_t y = 0,
                                                       int64_t guid = 0) {
  PROJECT_NAMESPACE_ID::DItemBasic basic;
  basic.set_type_id(type_id);
  basic.set_count(count);
  basic.set_guid(guid);
  basic.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
  basic.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
  return basic;
}

/// @brief 测试日志处理器: 默认只输出 Info 及以上到终端, Error/Warning 着重显示
///
///  - Debug 日志不输出 (默认只打印 Info 及以上)
///  - Error / Warning 输出到 stderr 并着色加粗
///  - Info 输出到 stdout
static void test_item_log_handler(const ItemLogRecord& record) {
  if (record.level < ItemLogLevel::kWarning) {
    return;
  }

  switch (record.level) {
    case ItemLogLevel::kError: {
      fprintf(stderr, "\033[1;31m[ERROR] %s:%d [%s] %s\033[0m\n", record.file_name, record.line_number,
              record.category.c_str(), record.message.c_str());
      break;
    }
    case ItemLogLevel::kWarning: {
      fprintf(stderr, "\033[1;33m[WARNING] %s:%d [%s] %s\033[0m\n", record.file_name, record.line_number,
              record.category.c_str(), record.message.c_str());
      break;
    }
    default: {
      fprintf(stdout, "[INFO] %s:%d [%s] %s\n", record.file_name, record.line_number, record.category.c_str(),
              record.message.c_str());
      break;
    }
  }
}

/// @brief 为测试 Grid 注册日志处理器 (默认只输出 Info 及以上)
static void register_test_log_handler(ItemGridAlgorithm& grid) {
  ItemLogHandler handler;
  handler.category = "ItemAlgorithm";
  handler.on_log = test_item_log_handler;
  grid.set_log_handler(handler);
}

/// @brief 初始化测试 Grid 算法 (inventory 类型, 默认 10x10)
static void init_test_grid(TestItemGridAlgorithm& grid, int32_t row = 10, int32_t col = 10,
                           int64_t container_guid = 0) {
  grid.init(ItemGridAlgorithmMode::kFiniteGrid, row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory,
            container_guid);
  // 注册配置
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
  // 注册测试日志处理器 (默认只输出 Info 及以上)
  register_test_log_handler(grid);
}

/// @brief 创建一个装备道具 DItemInstance (need_guid=true, 1x1, inventory 位置)
static PROJECT_NAMESPACE_ID::DItemInstance make_equip_item(int64_t guid, int32_t x, int32_t y) {
  PROJECT_NAMESPACE_ID::DItemInstance instance;
  auto* basic = instance.mutable_item_basic();
  basic->set_type_id(kEquipmentTypeId);
  basic->set_count(1);
  basic->set_guid(guid);
  basic->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
  basic->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
  return instance;
}

/// @brief 创建装备 Sub 请求 (按 GUID)
static PROJECT_NAMESPACE_ID::DItemBasic make_equip_sub_by_guid(int64_t guid) {
  PROJECT_NAMESPACE_ID::DItemBasic basic;
  basic.set_type_id(kEquipmentTypeId);
  basic.set_count(1);
  basic.set_guid(guid);
  return basic;
}

/// @brief 通过 foreach (Dump 接口) 导出 Grid 中所有条目
static std::vector<PROJECT_NAMESPACE_ID::DItemInstance> dump_grid_items(const TestItemGridAlgorithm& grid) {
  std::vector<PROJECT_NAMESPACE_ID::DItemInstance> items;
  grid.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    items.push_back(inst);
    return true;
  });
  return items;
}

/// @brief 在 Dump 结果中按 type_id + 位置查找条目 (占格道具)
static const PROJECT_NAMESPACE_ID::DItemInstance* find_dumped_by_position(
    const std::vector<PROJECT_NAMESPACE_ID::DItemInstance>& items, int32_t type_id, int32_t x, int32_t y) {
  for (const auto& inst : items) {
    if (inst.item_basic().type_id() == type_id &&
        inst.item_basic().position().grid_position().user_inventory().x() == x &&
        inst.item_basic().position().grid_position().user_inventory().y() == y) {
      return &inst;
    }
  }
  return nullptr;
}

/// @brief 创建 inventory 类型的目标位置
static PROJECT_NAMESPACE_ID::DItemPosition make_inventory_target(int32_t x, int32_t y) {
  PROJECT_NAMESPACE_ID::DItemPosition target;
  target.mutable_grid_position()->mutable_user_inventory()->set_x(x);
  target.mutable_grid_position()->mutable_user_inventory()->set_y(y);
  return target;
}

/// @brief 创建 backpack 类型的目标位置
static PROJECT_NAMESPACE_ID::DItemPosition make_backpack_target(int32_t x, int32_t y) {
  PROJECT_NAMESPACE_ID::DItemPosition target;
  target.mutable_grid_position()->mutable_character_inventory()->set_x(x);
  target.mutable_grid_position()->mutable_character_inventory()->set_y(y);
  return target;
}

/// @brief 在 Dump 结果中按 type_id + 背包位置查找条目
static const PROJECT_NAMESPACE_ID::DItemInstance* find_dumped_by_backpack_position(
    const std::vector<PROJECT_NAMESPACE_ID::DItemInstance>& items, int32_t type_id, int32_t x, int32_t y) {
  for (const auto& inst : items) {
    if (inst.item_basic().type_id() == type_id &&
        inst.item_basic().position().grid_position().character_inventory().x() == x &&
        inst.item_basic().position().grid_position().character_inventory().y() == y) {
      return &inst;
    }
  }
  return nullptr;
}

/// @brief 校验 get_item_count() 与 get_cached_item_count() 缓存计数一致
///
/// 在通过 get_item_count() 判断数量时, 额外校验缓存计数与实时计数相等
static void verify_item_count_consistency(const ItemGridAlgorithm& grid, int32_t type_id) {
  int64_t real_count = grid.get_item_count(type_id);
  int64_t cached_count = grid.get_cached_item_count(type_id);
  CASE_EXPECT_EQ(cached_count, real_count);
  if (cached_count != real_count) {
    CASE_MSG_ERROR() << "type_id=" << type_id << " real_count=" << real_count << " cached_count=" << cached_count;
  }
}

/// @brief 校验 find_entry_by_id 能找到指定 entry, 且数据符合预期
static void verify_find_entry_by_id(const TestItemGridAlgorithm& grid, uint64_t entry_id, int32_t type_id,
                                    int64_t count, int64_t guid) {
  auto found = grid.find_entry_by_id(entry_id);
  CASE_EXPECT_TRUE(found != nullptr);
  if (found) {
    CASE_EXPECT_EQ(found->entry_id(), entry_id);
    CASE_EXPECT_EQ(found->item_instance().item_basic().type_id(), type_id);
    CASE_EXPECT_EQ(found->item_instance().item_basic().count(), count);
    CASE_EXPECT_EQ(found->item_instance().item_basic().guid(), guid);
  } else {
    CASE_MSG_ERROR() << "entry_id " << entry_id << " should be found but not";
  }
}

/// @brief 校验 find_entry_by_id 找不到已删除/不存在的 entry
static void verify_not_find_entry_by_id(const TestItemGridAlgorithm& grid, uint64_t entry_id) {
  auto found = grid.find_entry_by_id(entry_id);
  CASE_EXPECT_TRUE(found == nullptr);
  if (found) {
    CASE_MSG_ERROR() << "entry_id " << entry_id << " should be removed but still found";
  }
}

/// @brief 通过 foreach (Dump 接口) 遍历 Grid 中所有条目, 验证数据一致性
///
/// 校验内容:
///   1. 每个条目 type_id > 0 且 count > 0
///   2. 占格道具可通过 get(position) 找回, 且字段一致
///   3. 各 type_id 的累计数量与 get_item_count() 一致
///   4. get_all_groups() 中无多余非空组
static void verify_grid_dump(const TestItemGridAlgorithm& grid) {
  std::unordered_map<int32_t, int64_t> type_counts;
  int total_entries = 0;

  grid.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    ++total_entries;
    int32_t type_id = inst.item_basic().type_id();
    int64_t count = inst.item_basic().count();

    // 基本字段合法性
    CASE_EXPECT_GT(type_id, 0);
    CASE_EXPECT_GT(count, static_cast<int64_t>(0));

    type_counts[type_id] += count;

    // 占格道具: 通过 position 反查, 且 type_id / count 一致
    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config && item_type_config->need_occupy_the_grid) {
      auto entry = grid.get(inst.item_basic().position().grid_position());
      CASE_EXPECT_TRUE(entry != nullptr);
      if (entry) {
        CASE_EXPECT_EQ(entry->item_instance().item_basic().type_id(), type_id);
        CASE_EXPECT_EQ(entry->item_instance().item_basic().count(), count);
      }
    }
    return true;
  });

  // 各类型累计数量 == get_item_count(), 且缓存计数一致
  for (const auto& pair : type_counts) {
    CASE_EXPECT_EQ(grid.get_item_count(pair.first), pair.second);
    verify_item_count_consistency(grid, pair.first);
  }

  // 反向: get_all_groups() 中出现的非空组必须在 foreach 中被统计到
  for (const auto& group_pair : grid.get_all_groups()) {
    int64_t foreach_count = 0;
    auto it = type_counts.find(group_pair.first);
    if (it != type_counts.end()) {
      foreach_count = it->second;
    }
    int64_t api_count = grid.get_item_count(group_pair.first);
    CASE_EXPECT_EQ(foreach_count, api_count);
    verify_item_count_consistency(grid, group_pair.first);
  }

  // find_entry_by_id 一致性: 所有现存 entry 都应能通过 entry_id 找到, 且数据一致
  for (const auto& group_pair : grid.get_all_groups()) {
    for (const auto& entry : group_pair.second) {
      if (!entry) {
        continue;
      }
      auto found = grid.find_entry_by_id(entry->entry_id());
      CASE_EXPECT_TRUE(found != nullptr);
      if (found) {
        CASE_EXPECT_EQ(found->entry_id(), entry->entry_id());
        CASE_EXPECT_EQ(found->item_instance().item_basic().type_id(), entry->item_instance().item_basic().type_id());
        CASE_EXPECT_EQ(found->item_instance().item_basic().count(), entry->item_instance().item_basic().count());
        CASE_EXPECT_EQ(found->item_instance().item_basic().guid(), entry->item_instance().item_basic().guid());
      }
    }
  }

  // is_empty() 与条目数一致
  if (total_entries == 0) {
    CASE_EXPECT_TRUE(grid.is_empty());
  } else {
    CASE_EXPECT_FALSE(grid.is_empty());
  }
}

/// @brief 辅助: 将 std::vector 形式的参数转换为 protobuf 类型, 并调用 apply_entries
static void call_apply_entries(
    TestItemGridAlgorithm& grid,
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
    const std::vector<uint64_t>& remove_ids,
    const std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>>& updates) {
  ::google::protobuf::RepeatedField<uint64_t> pb_remove_ids;
  for (uint64_t id : remove_ids) {
    pb_remove_ids.Add(id);
  }
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry> pb_updates;
  for (const auto& pair : updates) {
    auto* entry = pb_updates.Add();
    entry->set_entry_id(pair.first);
    *entry->mutable_instance() = pair.second;
  }
  grid.apply_entries(config_group, pb_remove_ids, pb_updates);
}

/// @brief 初始化 ServerTestItemGridAlgorithm (与 init_test_grid 相同配置)
static void init_server_grid(ServerTestItemGridAlgorithm& grid, int32_t row = 10, int32_t col = 10) {
  grid.init(ItemGridAlgorithmMode::kFiniteGrid, row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
  // 注册测试日志处理器 (默认只输出 Info 及以上)
  register_test_log_handler(grid);
}

/// @brief 服务器子类完成操作后, 将变更同步到客户端子类, 并验证双方数据一致
///
/// 步骤:
///   1. 用 ServerTestItemGridAlgorithm::collect_apply_data() 生成同步参数
///   2. 在 client 上执行 apply_entries
///   3. 对比 server 与 client 的:
///      a. 各 type_id 数量 (get_item_count)
///      b. 所有 entry (按 entry_id 逐条对比 type_id / count / guid)
///      c. 占格标记 (get_occupy_grid_flag)
///      d. 双方 verify_grid_dump
static void verify_server_client_sync(const ServerTestItemGridAlgorithm& server, const TestItemGridAlgorithm& client) {
  // 1. 比较 item_count_cache — 收集所有出现的 type_id
  const auto& server_groups = server.get_all_groups();
  const auto& client_groups = client.get_all_groups();

  std::set<int32_t> all_type_ids;
  for (const auto& pair : server_groups) {
    if (!pair.second.empty()) {
      all_type_ids.insert(pair.first);
    }
  }
  for (const auto& pair : client_groups) {
    if (!pair.second.empty()) {
      all_type_ids.insert(pair.first);
    }
  }

  for (int32_t tid : all_type_ids) {
    CASE_EXPECT_EQ(server.get_item_count(tid), client.get_item_count(tid));
    verify_item_count_consistency(server, tid);
    verify_item_count_consistency(client, tid);
  }

  // 2. 按 entry_id 逐条对比
  std::unordered_map<uint64_t, const ItemGridEntry*> server_entries, client_entries;
  for (const auto& group_pair : server_groups) {
    for (const auto& entry : group_pair.second) {
      if (entry) {
        server_entries[entry->entry_id()] = entry.get();
      }
    }
  }
  for (const auto& group_pair : client_groups) {
    for (const auto& entry : group_pair.second) {
      if (entry) {
        client_entries[entry->entry_id()] = entry.get();
      }
    }
  }

  CASE_EXPECT_EQ(server_entries.size(), client_entries.size());

  for (const auto& pair : server_entries) {
    uint64_t eid = pair.first;
    const ItemGridEntry* s_entry = pair.second;
    auto it = client_entries.find(eid);
    if (it == client_entries.end()) {
      CASE_MSG_ERROR() << "entry_id " << eid << " exists in server but not in client";
      continue;
    }
    const ItemGridEntry* c_entry = it->second;
    CASE_EXPECT_EQ(s_entry->item_instance().item_basic().type_id(), c_entry->item_instance().item_basic().type_id());
    CASE_EXPECT_EQ(s_entry->item_instance().item_basic().count(), c_entry->item_instance().item_basic().count());
    CASE_EXPECT_EQ(s_entry->item_instance().item_basic().guid(), c_entry->item_instance().item_basic().guid());
  }

  // 3. 对比 occupy_grid_flag
  const auto& server_flags = server.get_occupy_grid_flag();
  const auto& client_flags = client.get_occupy_grid_flag();
  CASE_EXPECT_EQ(server_flags.row_count(), client_flags.row_count());
  CASE_EXPECT_EQ(server_flags.column_count(), client_flags.column_count());
  for (size_t r = 0; r < server_flags.row_count() && r < client_flags.row_count(); ++r) {
    for (size_t c = 0; c < server_flags.column_count() && c < client_flags.column_count(); ++c) {
      bool server_occ = server_flags.is_occupied(static_cast<int32_t>(c), static_cast<int32_t>(r));
      bool client_occ = client_flags.is_occupied(static_cast<int32_t>(c), static_cast<int32_t>(r));
      if (server_occ != client_occ) {
        CASE_MSG_ERROR() << "occupy_grid_flag mismatch at (" << r << "," << c << ")"
                         << " server=" << server_occ << " client=" << client_occ;
      }
    }
  }

  // 4. 双方各自 verify_grid_dump
  verify_grid_dump(server);
  verify_grid_dump(client);
}

/// @brief 每一步服务器操作后: 收集变更 → 同步到客户端 → 验证双方一致
///
/// @param server  服务器 Grid (已完成操作)
/// @param client  客户端 Grid (每步增量同步)
/// @param config  共享 config_group
/// @param step_name 当前步骤描述 (用于错误信息定位)
static void sync_and_verify(ServerTestItemGridAlgorithm& server, TestItemGridAlgorithm& client,
                            const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config,
                            const char* step_name) {
  ::google::protobuf::RepeatedField<uint64_t> remove_ids;
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry> update_entries;
  server.collect_apply_data(remove_ids, update_entries);
  server.clear_change_cache();

  client.apply_entries(config, remove_ids, update_entries);

  // 验证双方一致
  verify_server_client_sync(server, client);

  // 额外: 打印步骤信息便于定位
  (void)step_name;
}

// ============================================================
// Container 辅助类
// ============================================================

ITEM_ALGORITHM_NAMESPACE_BEGIN
namespace item_algorithm {

/// @brief 测试用 Container, 只有一个 Grid
class TestItemGridContainer : public ItemGridContainer {
 public:
  atfw::util::memory::strong_rc_ptr<TestItemGridAlgorithm> grid;

  TestItemGridContainer(int32_t row = 10, int32_t col = 10) {
    grid = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    grid->init(ItemGridAlgorithmMode::kFiniteGrid, row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory,
               0);
    grid->register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    grid->register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
    // 注册测试日志处理器 (默认只输出 Info 及以上)
    ::register_test_log_handler(*grid);
  }

  item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& /*position*/) override {
    return grid;
  }
  item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& /*position*/) const override {
    return grid;
  }
};

/// @brief 双 Grid 测试容器: inventory_grid 处理 kUserInventory 位置, backpack_grid 处理 kCharacterInventory 位置
/// 用于测试跨 Grid Move 场景
class DualGridContainer : public ItemGridContainer {
 public:
  atfw::util::memory::strong_rc_ptr<TestItemGridAlgorithm> inventory_grid;  ///< 处理 kUserInventory 位置
  atfw::util::memory::strong_rc_ptr<TestItemGridAlgorithm> backpack_grid;   ///< 处理 kCharacterInventory 位置

  DualGridContainer(int32_t row = 10, int32_t col = 10) {
    inventory_grid = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    inventory_grid->init(ItemGridAlgorithmMode::kFiniteGrid, row, col,
                         PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    inventory_grid->register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    inventory_grid->register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
    ::register_test_log_handler(*inventory_grid);

    backpack_grid = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    backpack_grid->init(ItemGridAlgorithmMode::kFiniteGrid, row, col,
                        PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory, 0);
    backpack_grid->register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    backpack_grid->register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
    ::register_test_log_handler(*backpack_grid);
  }

  item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& pos) override {
    if (pos.grid_position().has_user_inventory()) {
      return inventory_grid;
    } else if (pos.grid_position().has_character_inventory()) {
      return backpack_grid;
    }
    return nullptr;
  }

  item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& pos) const override {
    if (pos.grid_position().has_user_inventory()) {
      return inventory_grid;
    } else if (pos.grid_position().has_character_inventory()) {
      return backpack_grid;
    }
    return nullptr;
  }
};

}  // namespace item_algorithm
ITEM_ALGORITHM_NAMESPACE_END

// ============================================================
// 用户游玩模拟 — 完整生命周期大测试
//
// 覆盖操作: check_add / add (1x1/2x2/装备GUID),
//           有位置 Grid 对无位置道具的拒绝 / stack overflow / position occupied / out of range,
//           check_sub / sub (部分/全部/按位置/按GUID/不足失败),
//           check_move / move (整体/部分拆分/目标占用失败),
//           load (占格/装备), foreach, clear,
//           entry_id (自增/独立/拆分产生新条目),
//           apply_entries (删除/更新/新增/位置变更/装备GUID),
//           Container (单Grid/双Grid跨Grid Move/大物品与小物品交换),
//           服务器→客户端同步验证
// ============================================================

CASE_TEST(ItemGridAlgorithm, user_gameplay_simulation) {
  auto server_ptr = atfw::util::memory::make_strong_rc<ServerTestItemGridAlgorithm>();
  auto& server = *server_ptr;
  init_server_grid(server);
  // 同时注册装备配置, 使服务器支持所有道具类型
  server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto client_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& client = *client_ptr;
  init_test_grid(client);
  client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto config = make_test_config_group();

  // ----------------------------------------------------------------
  // Step 1: 新手奖励 — 添加初始装备 (guid=1001)
  // 验证: check_add 通过, add 成功, Dump/Sync 正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 1: 新手奖励 ===\n";
  {
    auto equip = make_equip_item(1001, 0, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = equip;

    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.add(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 1);
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1001) != nullptr);
  CASE_EXPECT_FALSE(server.is_empty());
  // entry_id 应从 1 开始自增
  CASE_EXPECT_EQ(server.peek_next_entry_id(), static_cast<uint64_t>(2));
  {
    auto dumped = dump_grid_items(server);
    CASE_EXPECT_EQ(dumped.size(), static_cast<size_t>(1));
    auto* equip_d = find_dumped_by_position(dumped, kEquipmentTypeId, 0, 0);
    CASE_EXPECT_TRUE(equip_d != nullptr);
    if (equip_d) CASE_EXPECT_EQ(equip_d->item_basic().guid(), static_cast<int64_t>(1001));
  }
  // find_entry_by_id 校验: 初始装备可通过 entry_id 找到
  {
    uint64_t equip_eid = server.get_by_guid(1001)->entry_id();
    verify_find_entry_by_id(server, equip_eid, kEquipmentTypeId, 1, 1001);
    // 不存在的 entry_id 应找不到
    verify_not_find_entry_by_id(server, 0);
    verify_not_find_entry_by_id(server, equip_eid + 100);
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 1: 新手奖励");

  // ----------------------------------------------------------------
  // Step 2: 拾取材料 — 添加 1x1 道具到 (1,0) count=30, (2,0) count=50
  // 验证: 批量 add, 占格标记正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 2: 拾取材料 ===\n";
  {
    auto item1 = make_grid_item(kItemTypeId_1x1, 30, 1, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 50, 2, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = item1;
    *reqs.Add() = item2;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 80);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  // 验证占格标记: (0,0) 装备, (1,0) 1x1, (2,0) 1x1 => 三格占用
  {
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_TRUE(flags.is_occupied(0, 0));   // 装备
    CASE_EXPECT_TRUE(flags.is_occupied(1, 0));   // 1x1 at (1,0)
    CASE_EXPECT_TRUE(flags.is_occupied(2, 0));   // 1x1 at (2,0)
    CASE_EXPECT_FALSE(flags.is_occupied(3, 0));  // 空
    CASE_EXPECT_FALSE(flags.is_occupied(0, 1));  // 空
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 2: 拾取材料");

  // ----------------------------------------------------------------
  // Step 3: 有位置 Grid 不允许添加无位置道具
  // 验证: 新模式约束拒绝普通道具，现有占格数据不变
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 3: 有位置 Grid 拒绝无位置道具 ===\n";
  {
    ItemGridAddRequest reqs;
    *reqs.Add() = make_ungrid_item(kCoinTypeId, 200);
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    CASE_EXPECT_EQ(server.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  CASE_EXPECT_EQ(server.get_item_count(kCoinTypeId), 0);
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 3: 有位置 Grid 拒绝无位置道具");

  // ----------------------------------------------------------------
  // Step 4: 1x1 堆叠追加 — 在 (1,0) 追加 60 个 (已有 30, 合计 90, 上限 99)
  // 验证: 占格道具合并堆叠
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 4: 1x1 堆叠追加 ===\n";
  {
    auto item = make_grid_item(kItemTypeId_1x1, 60, 1, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 140);  // 90 + 50
  verify_item_count_consistency(server, kItemTypeId_1x1);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos10;
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos10);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) CASE_EXPECT_EQ(e->item_instance().item_basic().count(), static_cast<int64_t>(90));
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 4: 1x1 堆叠追加");

  // ----------------------------------------------------------------
  // Step 5: check_add 失败 — 堆叠溢出 / 位置占用 / 超出边界
  // 验证: 各种失败 error_code, 服务器数据不变
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 5: check_add 失败检查 ===\n";
  // 5a: stack overflow — (1,0) 已有 90, 再加 10 = 100 > 99
  {
    auto item = make_grid_item(kItemTypeId_1x1, 10, 1, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
    CASE_EXPECT_EQ(checked.result.failed_index, 0);
    // add 应直接返回错误
    auto result = server.add(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
  }
  // 5b: position occupied — (0,0) 已有装备
  {
    auto item = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 5c: out of range — (10,10) 超出 10x10 背包
  {
    auto item = make_grid_item(kItemTypeId_1x1, 1, 10, 10);
    ItemGridAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据应不变
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 140);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  verify_grid_dump(server);

  // ----------------------------------------------------------------
  // Step 6: 装备 GUID 相关失败检查
  // 验证: guid=0 失败, 重复 GUID 失败, 位置已被占用失败
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 6: 装备 GUID 添加失败检查 ===\n";
  // 6a: guid=0 应失败
  {
    auto bad = make_grid_item(kEquipmentTypeId, 1, 3, 0, 0);  // guid=0
    ItemGridAddRequest reqs;
    *reqs.Add() = bad;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 6b: 重复 GUID=1001 应失败
  {
    auto dup = make_equip_item(1001, 3, 0);  // GUID 1001 已存在
    ItemGridAddRequest reqs;
    *reqs.Add() = dup;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 6c: 位置 (0,0) 被装备 1001 占用
  {
    auto conflict = make_equip_item(9999, 0, 0);  // 新GUID但位置冲突
    ItemGridAddRequest reqs;
    *reqs.Add() = conflict;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  verify_grid_dump(server);

  // ----------------------------------------------------------------
  // Step 7: 添加 2x2 大物品 — 在 (4,4) 放一个 2x2 (accumulation_limit=1)
  // 验证: 2x2 占格标记正确 (4 格)
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 7: 添加 2x2 大物品 ===\n";
  {
    auto big = make_grid_item(kItemTypeId_2x2, 1, 4, 4);
    ItemGridAddRequest reqs;
    *reqs.Add() = big;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_2x2), 1);
  verify_item_count_consistency(server, kItemTypeId_2x2);
  {
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_TRUE(flags.is_occupied(4, 4));   // (4,4) row=4,col=4
    CASE_EXPECT_TRUE(flags.is_occupied(5, 4));   // (5,4) row=4,col=5
    CASE_EXPECT_TRUE(flags.is_occupied(4, 5));   // (4,5) row=5,col=4
    CASE_EXPECT_TRUE(flags.is_occupied(5, 5));   // (5,5) row=5,col=5
    CASE_EXPECT_FALSE(flags.is_occupied(3, 4));  // 旁边应为空
    CASE_EXPECT_FALSE(flags.is_occupied(4, 3));
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 7: 添加 2x2 大物品");

  // ----------------------------------------------------------------
  // Step 8: 获取更多装备 — guid=1002 在 (3,0), guid=1003 在 (4,0)
  // 验证: 多件装备, GUID 索引正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 8: 获取更多装备 ===\n";
  {
    auto e2 = make_equip_item(1002, 3, 0);
    auto e3 = make_equip_item(1003, 4, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = e2;
    *reqs.Add() = e3;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 3);
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1002) != nullptr);
  CASE_EXPECT_TRUE(server.get_by_guid(1003) != nullptr);
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 8: 获取更多装备");

  // ----------------------------------------------------------------
  // Step 9: 消费材料 — 扣减 (2,0) 上的 1x1 道具 20 个 (50→30)
  // 验证: check_sub 通过, 部分扣减, 不释放位置
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 9: 消费材料 (部分扣减) ===\n";
  {
    auto sub = make_sub_basic(kItemTypeId_1x1, 20, 2, 0);
    ItemGridSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.sub(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 120);  // 90 + 30
  verify_item_count_consistency(server, kItemTypeId_1x1);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos20);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) CASE_EXPECT_EQ(e->item_instance().item_basic().count(), static_cast<int64_t>(30));
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 9: 消费材料 (部分扣减)");

  // ----------------------------------------------------------------
  // Step 10: 消费材料 — 扣减 (2,0) 全部 30 个 (释放格子)
  // 验证: 完全扣减, 位置释放, 占格标记清除
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 10: 消费材料 (完全扣减释放位置) ===\n";
  uint64_t gpos20_entry_id = 0;
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos20);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) gpos20_entry_id = e->entry_id();
  }
  {
    auto sub = make_sub_basic(kItemTypeId_1x1, 30, 2, 0);
    ItemGridSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, std::move(reqs));
    server.sub(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);  // 只剩 (1,0)=90
  verify_item_count_consistency(server, kItemTypeId_1x1);
  // 完全扣减后, 该 entry 应无法通过 find_entry_by_id 找到
  verify_not_find_entry_by_id(server, gpos20_entry_id);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_TRUE(server.get(gpos20) == nullptr);  // 已释放
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_FALSE(flags.is_occupied(2, 0));  // (2,0) 已释放
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 10: 消费材料 (完全扣减)");

  // ----------------------------------------------------------------
  // Step 11: 扣减失败检查 — 数量不足 / 装备 GUID 不匹配
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 11: check_sub 失败检查 ===\n";
  // 11a: 扣减不足
  {
    auto sub = make_sub_basic(kItemTypeId_1x1, 999, 1, 0);
    ItemGridSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    // sub 应直接返回错误
    auto result = server.sub(checked);
    CASE_EXPECT_NE(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11b: 装备 guid=0 失败
  {
    auto sub = make_sub_basic(kEquipmentTypeId, 1, 0, 0, 0);
    ItemGridSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11c: 装备不存在的 GUID
  {
    auto sub = make_equip_sub_by_guid(77777);
    ItemGridSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11d: 装备 count != 1
  {
    PROJECT_NAMESPACE_ID::DItemBasic bad_sub;
    bad_sub.set_type_id(kEquipmentTypeId);
    bad_sub.set_count(2);
    bad_sub.set_guid(1001);
    ItemGridSubRequest reqs;
    *reqs.Add() = bad_sub;
    auto checked = server.check_sub(config, std::move(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 3);
  verify_item_count_consistency(server, kEquipmentTypeId);
  verify_grid_dump(server);

  // ----------------------------------------------------------------
  // Step 12: 删除装备 (按 GUID) — 卸下 guid=1002
  // 验证: GUID 索引清除, 位置释放
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 12: 按 GUID 扣减装备 ===\n";
  uint64_t equip1002_entry_id = 0;
  {
    auto by_guid = server.get_by_guid(1002);
    CASE_EXPECT_TRUE(by_guid != nullptr);
    if (by_guid) equip1002_entry_id = by_guid->entry_id();
  }
  {
    auto sub = make_equip_sub_by_guid(1002);
    ItemGridSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.sub(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 2);
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1002) == nullptr);
  // 装备删除后, 应无法通过 find_entry_by_id 找到
  verify_not_find_entry_by_id(server, equip1002_entry_id);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos30;
    gpos30.mutable_user_inventory()->set_x(3);
    gpos30.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_TRUE(server.get(gpos30) == nullptr);
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 12: 按 GUID 扣减装备");

  // ----------------------------------------------------------------
  // Step 13: Move — 将 (1,0) 上的 1x1 道具整体移动到 (2,0) (已空出)
  // 验证: 旧位置释放, 新位置占用, entry_id 变化
  // ----------------------------------------------------------------
  {
    uint64_t old_entry_id = 0;
    CASE_MSG_INFO() << "=== Step 13: Move 整体搬移 ===\n";
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition gpos10;
      gpos10.mutable_user_inventory()->set_x(1);
      gpos10.mutable_user_inventory()->set_y(0);
      auto entry = server.get(gpos10);
      CASE_EXPECT_TRUE(entry != nullptr);
      old_entry_id = entry->entry_id();
      ItemGridMoveRequest move_req;
      move_req.move_sub_entrys.push_back({entry, 90});

      PROJECT_NAMESPACE_ID::DItemPosition goal;
      goal.mutable_grid_position()->mutable_user_inventory()->set_x(2);
      goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
      move_req.move_add_entrys.push_back({entry, goal, 90});

      auto checked = server.check_move(config, std::move(move_req));
      CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      auto result = server.move(checked);
      CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    }
    CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);
    verify_item_count_consistency(server, kItemTypeId_1x1);
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition gpos10, gpos20;
      gpos10.mutable_user_inventory()->set_x(1);
      gpos10.mutable_user_inventory()->set_y(0);
      gpos20.mutable_user_inventory()->set_x(2);
      gpos20.mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(server.get(gpos10) == nullptr);  // 旧位置空
      auto moved = server.get(gpos20);
      CASE_EXPECT_TRUE(moved != nullptr);  // 新位置有
      if (moved) {
        CASE_EXPECT_EQ(moved->item_instance().item_basic().count(), static_cast<int64_t>(90));
        // 整体 Move (sub 全部+add) 会创建新 entry, entry_id 不同
        CASE_EXPECT_NE(moved->entry_id(), old_entry_id);
        // 新 entry 应能通过 find_entry_by_id 找到
        verify_find_entry_by_id(server, moved->entry_id(), kItemTypeId_1x1, 90, 0);
      }
      const auto& flags = server.get_occupy_grid_flag();
      CASE_EXPECT_FALSE(flags.is_occupied(1, 0));  // (1,0) 已释放
      CASE_EXPECT_TRUE(flags.is_occupied(2, 0));   // (2,0) 占用
    }
    // 整体 Move 后: 旧 entry 已删除, 应无法通过 find_entry_by_id 找到
    verify_not_find_entry_by_id(server, old_entry_id);
    verify_grid_dump(server);
    sync_and_verify(server, client, config, "Step 13: Move 整体搬移");
  }

  // ----------------------------------------------------------------
  // Step 14: Move 部分拆分 — 从 (2,0) 取 40 个到 (5,0)
  // 验证: 源减少, 目标新 entry, entry_id 不同
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 14: Move 部分拆分 ===\n";
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto entry = server.get(gpos20);
    CASE_EXPECT_TRUE(entry != nullptr);

    ItemGridMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 40});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(5);
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 40});

    auto checked = server.check_move(config, std::move(move_req));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.move(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);  // 总量不变
  verify_item_count_consistency(server, kItemTypeId_1x1);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20, gpos50;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    gpos50.mutable_user_inventory()->set_x(5);
    gpos50.mutable_user_inventory()->set_y(0);
    auto src = server.get(gpos20);
    auto dst = server.get(gpos50);
    CASE_EXPECT_TRUE(src != nullptr);
    CASE_EXPECT_TRUE(dst != nullptr);
    if (src) CASE_EXPECT_EQ(src->item_instance().item_basic().count(), static_cast<int64_t>(50));
    if (dst) CASE_EXPECT_EQ(dst->item_instance().item_basic().count(), static_cast<int64_t>(40));
    // 拆分产生新 entry_id
    if (src && dst) CASE_EXPECT_NE(src->entry_id(), dst->entry_id());
    // 拆分 Move 后: 源/目标 entry 都应能通过 find_entry_by_id 找到
    if (src) verify_find_entry_by_id(server, src->entry_id(), kItemTypeId_1x1, 50, 0);
    if (dst) verify_find_entry_by_id(server, dst->entry_id(), kItemTypeId_1x1, 40, 0);
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 14: Move 部分拆分");

  // ----------------------------------------------------------------
  // Step 15: Move 失败 — 目标位置被占用
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 15: Move 目标位置被占用失败 ===\n";
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto entry = server.get(gpos20);
    CASE_EXPECT_TRUE(entry != nullptr);

    ItemGridMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 10});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(5);  // (5,0) 已被 step14 占
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 10});

    auto checked = server.check_move(config, std::move(move_req));
    // 目标已有不同类型的entry (但同type允许合并), 这里同type应合并
    // 实际上 (5,0) 已有 40 个 1x1, 合并后 50, 不超 99, 应成功
    // 这里测试 Move 到已有装备的位置 (不同type, 不可合并)
  }
  // 改用 move 到装备位置 (0,0)
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto entry = server.get(gpos20);
    CASE_EXPECT_TRUE(entry != nullptr);

    ItemGridMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 10});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(0);  // (0,0) 是装备
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 10});

    auto checked = server.check_move(config, std::move(move_req));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  verify_grid_dump(server);

  // ----------------------------------------------------------------
  // Step 16: Move 装备位置
  // 验证: 装备 guid=1003 从 (4,0) 移到 (3,0) (已空出)
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 16: Move 装备 ===\n";
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos40;
    gpos40.mutable_user_inventory()->set_x(4);
    gpos40.mutable_user_inventory()->set_y(0);
    auto entry = server.get(gpos40);
    CASE_EXPECT_TRUE(entry != nullptr);
    CASE_EXPECT_EQ(entry->item_instance().item_basic().guid(), static_cast<int64_t>(1003));

    ItemGridMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 1});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(3);
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 1});

    auto checked = server.check_move(config, std::move(move_req));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.move(checked);
  }
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos40, gpos30;
    gpos40.mutable_user_inventory()->set_x(4);
    gpos40.mutable_user_inventory()->set_y(0);
    gpos30.mutable_user_inventory()->set_x(3);
    gpos30.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_TRUE(server.get(gpos40) == nullptr);
    auto moved_eq = server.get(gpos30);
    CASE_EXPECT_TRUE(moved_eq != nullptr);
    if (moved_eq) CASE_EXPECT_EQ(moved_eq->item_instance().item_basic().guid(), static_cast<int64_t>(1003));
    // GUID 索引仍有效
    auto by_guid = server.get_by_guid(1003);
    CASE_EXPECT_TRUE(by_guid != nullptr);
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 16: Move 装备");

  // ----------------------------------------------------------------
  // ----------------------------------------------------------------
  // Step 18: Load 接口 — 模拟从数据库加载存档
  // 新建一个独立的 server/client Grid, Load 占格道具与装备, 验证同步
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 18: Load 从数据库恢复 ===\n";
  {
    // 用独立 Grid 测试 Load
    auto load_server_ptr = atfw::util::memory::make_strong_rc<ServerTestItemGridAlgorithm>();
    auto& load_server = *load_server_ptr;
    init_server_grid(load_server);
    load_server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    auto load_client_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& load_client = *load_client_ptr;
    init_test_grid(load_client);
    load_client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    // Load 1x1 在 (0,0)
    auto item = make_grid_item(kItemTypeId_1x1, 55, 0, 0);
    CASE_EXPECT_TRUE(load_server.load(config, item));

    // Load 装备 guid=2001
    auto equip = make_equip_item(2001, 1, 1);
    CASE_EXPECT_TRUE(load_server.load(config, equip));

    // 同位置同类型 (无 GUID) 的 Load 会堆叠合并, 不会失败
    auto dup = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    CASE_EXPECT_TRUE(load_server.load(config, dup));  // 合并到 55+10=65

    // 重复 Load 同 GUID 应失败
    auto equip_dup = make_equip_item(2001, 2, 2);
    CASE_EXPECT_FALSE(load_server.load(config, equip_dup));

    CASE_EXPECT_EQ(load_server.get_item_count(kItemTypeId_1x1), 65);  // 55+10 堆叠
    verify_item_count_consistency(load_server, kItemTypeId_1x1);
    CASE_EXPECT_EQ(load_server.get_item_count(kEquipmentTypeId), 1);
    verify_item_count_consistency(load_server, kEquipmentTypeId);
    CASE_EXPECT_TRUE(load_server.get_by_guid(2001) != nullptr);
    // find_entry_by_id 校验: Load 的条目都应能按 entry_id 找到
    {
      uint64_t item_eid = (*load_server.get_group(kItemTypeId_1x1)->begin())->entry_id();
      uint64_t equip_eid = load_server.get_by_guid(2001)->entry_id();
      verify_find_entry_by_id(load_server, item_eid, kItemTypeId_1x1, 65, 0);
      verify_find_entry_by_id(load_server, equip_eid, kEquipmentTypeId, 1, 2001);
    }

    verify_grid_dump(load_server);
    sync_and_verify(load_server, load_client, config, "Step 18: Load");
  }

  // ----------------------------------------------------------------
  // Step 19: foreach + clear — 清空后验证
  // 用独立 Grid 测试
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 19: foreach + clear ===\n";
  {
    auto temp_ptr = atfw::util::memory::make_strong_rc<ServerTestItemGridAlgorithm>();
    auto& temp = *temp_ptr;
    init_server_grid(temp);

    // 添加若干道具
    auto item1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = item1;
    *reqs.Add() = item2;
    auto checked = temp.check_add(config, std::move(reqs));
    temp.add(checked);

    // 记录添加的 entry_id, 用于后续 find_entry_by_id 校验
    uint64_t temp_eid1 = 0, temp_eid2 = 0;
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition g00, g10;
      g00.mutable_user_inventory()->set_x(0);
      g00.mutable_user_inventory()->set_y(0);
      g10.mutable_user_inventory()->set_x(1);
      g10.mutable_user_inventory()->set_y(0);
      auto e1 = temp.get(g00);
      auto e2 = temp.get(g10);
      CASE_EXPECT_TRUE(e1 != nullptr);
      CASE_EXPECT_TRUE(e2 != nullptr);
      if (e1) temp_eid1 = e1->entry_id();
      if (e2) temp_eid2 = e2->entry_id();
      // clear 前, 各 entry 都应能找到
      verify_find_entry_by_id(temp, temp_eid1, kItemTypeId_1x1, 10, 0);
      verify_find_entry_by_id(temp, temp_eid2, kItemTypeId_1x1, 20, 0);
    }

    // foreach 计数
    int count = 0;
    temp.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance&) {
      ++count;
      return true;
    });
    CASE_EXPECT_EQ(count, 2);

    // clear
    temp.clear();
    CASE_EXPECT_TRUE(temp.is_empty());
    CASE_EXPECT_EQ(temp.get_item_count(kItemTypeId_1x1), 0);
    verify_item_count_consistency(temp, kItemTypeId_1x1);
    // clear 后, 所有 entry 都应无法通过 find_entry_by_id 找到
    verify_not_find_entry_by_id(temp, temp_eid1);
    verify_not_find_entry_by_id(temp, temp_eid2);
    // 占格标记应全部清除
    {
      const auto& flags = temp.get_occupy_grid_flag();
      for (size_t r = 0; r < flags.row_count(); ++r) {
        for (size_t c = 0; c < flags.column_count(); ++c) {
          CASE_EXPECT_FALSE(flags.is_occupied(static_cast<int32_t>(c), static_cast<int32_t>(r)));
        }
      }
    }
    verify_grid_dump(temp);
  }

  // ----------------------------------------------------------------
  // Step 20: entry_id 独立性 — 两个 Grid 各自自增
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 20: entry_id 独立性验证 ===\n";
  {
    auto grid_a_ptr = atfw::util::memory::make_strong_rc<ServerTestItemGridAlgorithm>();
    auto& grid_a = *grid_a_ptr;
    init_server_grid(grid_a);
    auto grid_b_ptr = atfw::util::memory::make_strong_rc<ServerTestItemGridAlgorithm>();
    auto& grid_b = *grid_b_ptr;
    init_server_grid(grid_b);

    auto item_a = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    ItemGridAddRequest ra;
    *ra.Add() = item_a;
    {
      auto checked = grid_a.check_add(config, std::move(ra));
      grid_a.add(checked);
    }
    ItemGridAddRequest rb_from_a;
    *rb_from_a.Add() = item_a;
    {
      auto checked = grid_b.check_add(config, std::move(rb_from_a));
      grid_b.add(checked);
    }

    auto item_b = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    ItemGridAddRequest rb;
    *rb.Add() = item_b;
    auto checked_b = grid_a.check_add(config, std::move(rb));
    grid_b.add(checked_b);

    // 两个 Grid 的 entry_id 独立
    PROJECT_NAMESPACE_ID::DItemGridPosition gp00;
    gp00.mutable_user_inventory()->set_x(0);
    gp00.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_EQ(grid_a.get(gp00)->entry_id(), static_cast<uint64_t>(1));
    CASE_EXPECT_EQ(grid_b.get(gp00)->entry_id(), static_cast<uint64_t>(1));
  }

  // ----------------------------------------------------------------
  // Step 21: apply_entries 直接测试 — 删除 / 更新 / 新增 / 位置变更
  // 在独立 Grid 上验证, 不通过 Server→Client 流程
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 21: apply_entries 直接测试 ===\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    init_test_grid(grid);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    // 添加几个道具
    auto item1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
    auto item3 = make_grid_item(kItemTypeId_1x1, 30, 2, 0);
    auto equip = make_equip_item(3001, 3, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = item1;
    *reqs.Add() = item2;
    *reqs.Add() = item3;
    *reqs.Add() = equip;
    auto result = grid.check_add(config, std::move(reqs));
    grid.add(result);

    PROJECT_NAMESPACE_ID::DItemGridPosition gpos00, gpos10, gpos20, gpos30;
    gpos00.mutable_user_inventory()->set_x(0);
    gpos00.mutable_user_inventory()->set_y(0);
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    gpos30.mutable_user_inventory()->set_x(3);
    gpos30.mutable_user_inventory()->set_y(0);

    uint64_t eid1 = grid.get(gpos00)->entry_id();
    uint64_t eid2 = grid.get(gpos10)->entry_id();
    uint64_t eid3 = grid.get(gpos20)->entry_id();
    uint64_t eid_eq = grid.get(gpos30)->entry_id();

    // 21a: 删除 item1 和 item3
    {
      std::vector<uint64_t> rm = {eid1, eid3};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd;
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get(gpos00) == nullptr);
    CASE_EXPECT_TRUE(grid.get(gpos20) == nullptr);
    CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 20);  // 只剩 eid2
    verify_item_count_consistency(grid, kItemTypeId_1x1);
    // 21a: 被删除的 entry 应找不到, 保留的应能找到
    verify_not_find_entry_by_id(grid, eid1);
    verify_not_find_entry_by_id(grid, eid3);
    verify_find_entry_by_id(grid, eid2, kItemTypeId_1x1, 20, 0);

    // 21b: 更新 eid2 count 20→88
    {
      std::vector<uint64_t> rm;
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {eid2, make_grid_item(kItemTypeId_1x1, 88, 1, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_EQ(grid.get(gpos10)->item_instance().item_basic().count(), static_cast<int64_t>(88));
    CASE_EXPECT_EQ(grid.get(gpos10)->entry_id(), eid2);
    // 21b: 更新后 entry_id 不变, 数据应更新
    verify_find_entry_by_id(grid, eid2, kItemTypeId_1x1, 88, 0);

    // 21c: 位置变更 — eid2 从 (1,0) → (7,8)
    {
      std::vector<uint64_t> rm;
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {eid2, make_grid_item(kItemTypeId_1x1, 88, 7, 8)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get(gpos10) == nullptr);
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos78;
    gpos78.mutable_user_inventory()->set_x(7);
    gpos78.mutable_user_inventory()->set_y(8);
    CASE_EXPECT_TRUE(grid.get(gpos78) != nullptr);
    if (grid.get(gpos78)) CASE_EXPECT_EQ(grid.get(gpos78)->entry_id(), eid2);
    // 21c: 位置变更不改变 entry_id, 仍可找到
    verify_find_entry_by_id(grid, eid2, kItemTypeId_1x1, 88, 0);

    // 21d: 新增 entry (entry_id=500)
    {
      std::vector<uint64_t> rm;
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {500, make_grid_item(kItemTypeId_1x1, 25, 0, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get(gpos00) != nullptr);
    if (grid.get(gpos00)) CASE_EXPECT_EQ(grid.get(gpos00)->entry_id(), static_cast<uint64_t>(500));
    // 21d: 新增 entry 应能找到
    verify_find_entry_by_id(grid, 500, kItemTypeId_1x1, 25, 0);

    // 21e: 删除后同位置新增 (替换)
    {
      uint64_t old_eid = grid.get(gpos00)->entry_id();
      std::vector<uint64_t> rm = {old_eid};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {999, make_grid_item(kItemTypeId_1x1, 42, 0, 0)}};
      call_apply_entries(grid, config, rm, upd);
      // 21e: 被替换的旧 entry 应找不到
      verify_not_find_entry_by_id(grid, old_eid);
    }
    CASE_EXPECT_TRUE(grid.get(gpos00) != nullptr);
    if (grid.get(gpos00)) {
      CASE_EXPECT_EQ(grid.get(gpos00)->entry_id(), static_cast<uint64_t>(999));
      CASE_EXPECT_EQ(grid.get(gpos00)->item_instance().item_basic().count(), static_cast<int64_t>(42));
      // 21e: 新 entry 应能找到
      verify_find_entry_by_id(grid, 999, kItemTypeId_1x1, 42, 0);
    }

    // 21f: 装备 GUID — 删除 + 新增
    {
      std::vector<uint64_t> rm = {eid_eq};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {{777, make_equip_item(3002, 3, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get_by_guid(3001) == nullptr);
    CASE_EXPECT_TRUE(grid.get_by_guid(3002) != nullptr);
    if (grid.get_by_guid(3002)) CASE_EXPECT_EQ(grid.get_by_guid(3002)->entry_id(), static_cast<uint64_t>(777));
    // 21f: 被删除的装备应找不到, 新装备应能找到
    verify_not_find_entry_by_id(grid, eid_eq);
    verify_find_entry_by_id(grid, 777, kEquipmentTypeId, 1, 3002);

    verify_grid_dump(grid);
  }

  // ----------------------------------------------------------------
  // Step 22: Container 单 Grid — check_add / add / check_sub / sub
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 22: Container 单 Grid ===\n";
  {
    auto container_ptr = atfw::util::memory::make_strong_rc<TestItemGridContainer>();
    auto& container = *container_ptr;

    // add
    auto item = make_grid_item(kItemTypeId_1x1, 5, 1, 1);
    ItemGridAddRequest add_reqs;
    *add_reqs.Add() = item;
    auto checked_add = container.check_add(config, std::move(add_reqs));
    CASE_EXPECT_EQ(checked_add.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto add_result = container.add(checked_add);
    CASE_EXPECT_EQ(add_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(container.grid->get_item_count(kItemTypeId_1x1), 5);
    verify_item_count_consistency(*container.grid, kItemTypeId_1x1);
    // find_entry_by_id 校验: add 后 entry 应能找到
    uint64_t container_eid = 0;
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition gpos11;
      gpos11.mutable_user_inventory()->set_x(1);
      gpos11.mutable_user_inventory()->set_y(1);
      auto e = container.grid->get(gpos11);
      CASE_EXPECT_TRUE(e != nullptr);
      if (e) {
        container_eid = e->entry_id();
        verify_find_entry_by_id(*container.grid, container_eid, kItemTypeId_1x1, 5, 0);
      }
    }

    // sub
    auto sub = make_sub_basic(kItemTypeId_1x1, 3, 1, 1);
    ItemGridSubRequest sub_reqs;
    *sub_reqs.Add() = sub;
    auto checked_sub = container.check_sub(config, std::move(sub_reqs));
    CASE_EXPECT_EQ(checked_sub.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto sub_result = container.sub(checked_sub);
    CASE_EXPECT_EQ(sub_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(container.grid->get_item_count(kItemTypeId_1x1), 2);
    verify_item_count_consistency(*container.grid, kItemTypeId_1x1);
    // find_entry_by_id 校验: 部分扣减后 entry 仍在, count 更新为 2
    verify_find_entry_by_id(*container.grid, container_eid, kItemTypeId_1x1, 2, 0);

    // check_add 失败: stack overflow
    auto overflow = make_grid_item(kItemTypeId_1x1, 100, 1, 1);
    ItemGridAddRequest fail_reqs;
    *fail_reqs.Add() = overflow;
    auto fail_checked = container.check_add(config, std::move(fail_reqs));
    CASE_EXPECT_EQ(fail_checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
    auto fail_result = container.add(fail_checked);
    CASE_EXPECT_EQ(fail_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);

    // check_sub 失败: 空背包扣减不存在的道具
    auto bad_sub = make_sub_basic(kVirtualTypeId, 10);
    ItemGridSubRequest fail_sub_reqs;
    *fail_sub_reqs.Add() = bad_sub;
    auto fail_sub_checked = container.check_sub(config, std::move(fail_sub_reqs));
    CASE_EXPECT_NE(fail_sub_checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);

    verify_grid_dump(*container.grid);
  }

  // ----------------------------------------------------------------
  // Step 23: Container Move — 同位置跳过 / 合并操作 / 大小物品交换
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 23: Container Move 系列 ===\n";
  {
    auto container_ptr = atfw::util::memory::make_strong_rc<TestItemGridContainer>();
    auto& container = *container_ptr;

    // 放两个 1x1：(0,0)=10, (1,0)=20
    {
      auto i1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
      auto i2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
      ItemGridAddRequest reqs;
      *reqs.Add() = i1;
      *reqs.Add() = i2;
      auto checked = container.check_add(config, std::move(reqs));
      container.add(checked);
    }

    // 23a: same position skip
    {
      PROJECT_NAMESPACE_ID::DItemBasic sub_basic = make_sub_basic(kItemTypeId_1x1, 5, 0, 0);
      std::vector<ItemGridContainerMoveRequest> move_reqs;
      move_reqs.push_back({sub_basic, make_inventory_target(0, 0)});
      auto checked = container.check_move(config, std::move(move_reqs));
      CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
      // 应被优化跳过
      auto result = container.move(checked);
      CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      CASE_EXPECT_EQ(container.grid->get_item_count(kItemTypeId_1x1), 30);  // 不变
      verify_item_count_consistency(*container.grid, kItemTypeId_1x1);
    }

    // 23b: merge sub — 两个 move 请求操作同一源
    {
      PROJECT_NAMESPACE_ID::DItemBasic sub1 = make_sub_basic(kItemTypeId_1x1, 3, 0, 0);
      PROJECT_NAMESPACE_ID::DItemBasic sub2 = make_sub_basic(kItemTypeId_1x1, 2, 0, 0);
      std::vector<ItemGridContainerMoveRequest> move_reqs;
      move_reqs.push_back({sub1, make_inventory_target(2, 0)});
      move_reqs.push_back({sub2, make_inventory_target(3, 0)});
      auto checked = container.check_move(config, std::move(move_reqs));
      CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
      container.move(checked);
    }
    // (0,0)=5, (1,0)=20, (2,0)=3, (3,0)=2
    CASE_EXPECT_EQ(container.grid->get_item_count(kItemTypeId_1x1), 30);
    verify_item_count_consistency(*container.grid, kItemTypeId_1x1);
    {
      auto dumped = dump_grid_items(*container.grid);
      auto* at00 = find_dumped_by_position(dumped, kItemTypeId_1x1, 0, 0);
      auto* at20 = find_dumped_by_position(dumped, kItemTypeId_1x1, 2, 0);
      auto* at30 = find_dumped_by_position(dumped, kItemTypeId_1x1, 3, 0);
      CASE_EXPECT_TRUE(at00 != nullptr);
      CASE_EXPECT_TRUE(at20 != nullptr);
      CASE_EXPECT_TRUE(at30 != nullptr);
      if (at00) CASE_EXPECT_EQ(at00->item_basic().count(), static_cast<int64_t>(5));
      if (at20) CASE_EXPECT_EQ(at20->item_basic().count(), static_cast<int64_t>(3));
      if (at30) CASE_EXPECT_EQ(at30->item_basic().count(), static_cast<int64_t>(2));
    }
    verify_grid_dump(*container.grid);
  }

  // ----------------------------------------------------------------
  // Step 24: Container — 大物品与小物品交换位置
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 24: 大物品与小物品交换位置 ===\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridContainer>();
    auto& container = *grid_ptr;

    // 放 2x2 在 (0,0) 和 1x1 在 (4,0), (5,0)
    auto big = make_grid_item(kItemTypeId_2x2, 1, 0, 0);
    auto s1 = make_grid_item(kItemTypeId_1x1, 10, 4, 0);
    auto s2 = make_grid_item(kItemTypeId_1x1, 20, 5, 0);
    {
      ItemGridAddRequest reqs;
      *reqs.Add() = big;
      *reqs.Add() = s1;
      *reqs.Add() = s2;
      auto checked = container.check_add(config, std::move(reqs));
      container.add(checked);
    }

    // 交换: 2x2(0,0) → (4,0), 两个 1x1 → (0,0) 和 (1,0)
    PROJECT_NAMESPACE_ID::DItemBasic big_sub = make_sub_basic(kItemTypeId_2x2, 1, 0, 0);
    PROJECT_NAMESPACE_ID::DItemBasic s1_sub = make_sub_basic(kItemTypeId_1x1, 10, 4, 0);
    PROJECT_NAMESPACE_ID::DItemBasic s2_sub = make_sub_basic(kItemTypeId_1x1, 20, 5, 0);
    std::vector<ItemGridContainerMoveRequest> move_reqs;
    move_reqs.push_back({big_sub, make_inventory_target(4, 0)});
    move_reqs.push_back({s1_sub, make_inventory_target(0, 0)});
    move_reqs.push_back({s2_sub, make_inventory_target(1, 0)});

    auto checked = container.check_move(config, std::move(move_reqs));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = container.move(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 验证
    {
      auto dumped = dump_grid_items(*container.grid);
      auto* big_at40 = find_dumped_by_position(dumped, kItemTypeId_2x2, 4, 0);
      auto* s1_at00 = find_dumped_by_position(dumped, kItemTypeId_1x1, 0, 0);
      auto* s2_at10 = find_dumped_by_position(dumped, kItemTypeId_1x1, 1, 0);
      CASE_EXPECT_TRUE(big_at40 != nullptr);
      CASE_EXPECT_TRUE(s1_at00 != nullptr);
      CASE_EXPECT_TRUE(s2_at10 != nullptr);
    }
    // 占格标记验证
    {
      const auto& flags = container.grid->get_occupy_grid_flag();
      CASE_EXPECT_TRUE(flags.is_occupied(0, 0));   // 1x1 at (0,0)
      CASE_EXPECT_TRUE(flags.is_occupied(1, 0));   // 1x1 at (1,0)
      CASE_EXPECT_FALSE(flags.is_occupied(0, 1));  // 原来 2x2 占 (0,0)~(1,1), 现在释放
      CASE_EXPECT_TRUE(flags.is_occupied(4, 0));   // 2x2 at (4,0)
      CASE_EXPECT_TRUE(flags.is_occupied(5, 0));   // 2x2 at (5,0)→col=5
      CASE_EXPECT_TRUE(flags.is_occupied(4, 1));   // 2x2 row=1
      CASE_EXPECT_TRUE(flags.is_occupied(5, 1));
    }
    verify_grid_dump(*container.grid);
  }

  // ----------------------------------------------------------------
  // Step 25: Container — 跨 Grid Move (DualGridContainer)
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 25: 跨 Grid Move ===\n";
  {
    auto container_ptr = atfw::util::memory::make_strong_rc<DualGridContainer>();
    auto& container = *container_ptr;

    // inventory_grid 添加 1x1 (2,3) count=10
    {
      auto item = make_grid_item(kItemTypeId_1x1, 10, 2, 3);
      ItemGridAddRequest reqs;
      *reqs.Add() = item;
      auto checked = container.check_add(config, std::move(reqs));
      container.add(checked);
    }
    CASE_EXPECT_EQ(container.inventory_grid->get_item_count(kItemTypeId_1x1), 10);
    verify_item_count_consistency(*container.inventory_grid, kItemTypeId_1x1);

    // 跨 Grid: inventory (2,3) → backpack (0,0), 6 个
    PROJECT_NAMESPACE_ID::DItemBasic src_basic = make_sub_basic(kItemTypeId_1x1, 6, 2, 3);
    std::vector<ItemGridContainerMoveRequest> move_reqs;
    move_reqs.push_back({src_basic, make_backpack_target(0, 0)});

    auto checked = container.check_move(config, std::move(move_reqs));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    // CASE_EXPECT_EQ(checked.grid_data.size(), static_cast<size_t>(2));

    auto result = container.move(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 验证
    CASE_EXPECT_EQ(container.inventory_grid->get_item_count(kItemTypeId_1x1), 4);
    verify_item_count_consistency(*container.inventory_grid, kItemTypeId_1x1);
    CASE_EXPECT_EQ(container.backpack_grid->get_item_count(kItemTypeId_1x1), 6);
    verify_item_count_consistency(*container.backpack_grid, kItemTypeId_1x1);
    // find_entry_by_id 校验: 源 entry 仍在(数量减少), 目标为新 entry, 各 Grid 索引相互独立
    {
      uint64_t src_eid = (*container.inventory_grid->get_group(kItemTypeId_1x1)->begin())->entry_id();
      uint64_t dst_eid = (*container.backpack_grid->get_group(kItemTypeId_1x1)->begin())->entry_id();
      verify_find_entry_by_id(*container.inventory_grid, src_eid, kItemTypeId_1x1, 4, 0);
      verify_find_entry_by_id(*container.backpack_grid, dst_eid, kItemTypeId_1x1, 6, 0);
    }
    {
      auto inv_dumped = dump_grid_items(*container.inventory_grid);
      auto* at23 = find_dumped_by_position(inv_dumped, kItemTypeId_1x1, 2, 3);
      CASE_EXPECT_TRUE(at23 != nullptr);
      if (at23) CASE_EXPECT_EQ(at23->item_basic().count(), static_cast<int64_t>(4));
    }
    {
      auto bp_dumped = dump_grid_items(*container.backpack_grid);
      auto* at00 = find_dumped_by_backpack_position(bp_dumped, kItemTypeId_1x1, 0, 0);
      CASE_EXPECT_TRUE(at00 != nullptr);
      if (at00) CASE_EXPECT_EQ(at00->item_basic().count(), static_cast<int64_t>(6));
    }
    // backpack 第一个 entry_id 应为 1
    PROJECT_NAMESPACE_ID::DItemGridPosition bp00;
    bp00.mutable_character_inventory()->set_x(0);
    bp00.mutable_character_inventory()->set_y(0);
    auto bp_entry = container.backpack_grid->get(bp00);
    CASE_EXPECT_TRUE(bp_entry != nullptr);
    if (bp_entry) CASE_EXPECT_EQ(bp_entry->entry_id(), static_cast<uint64_t>(1));

    verify_grid_dump(*container.inventory_grid);
    verify_grid_dump(*container.backpack_grid);
  }

  // ----------------------------------------------------------------
  // Step 26: 继续主线 — 回到主 server/client, 再获得一波占格奖励后验证整体状态
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 26: 继续主线 — 最终奖励 ===\n";
  {
    auto new_item = make_grid_item(kItemTypeId_1x1, 50, 1, 0);  // (1,0) 已空
    auto new_equip = make_equip_item(1004, 6, 0);
    ItemGridAddRequest reqs;
    *reqs.Add() = new_item;
    *reqs.Add() = new_equip;
    auto checked = server.check_add(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 140);  // 50(2,0) + 40(5,0) + 50(1,0)
  verify_item_count_consistency(server, kItemTypeId_1x1);
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 3);  // 1001, 1003, 1004
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1004) != nullptr);
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 26: 最终奖励");

  // ----------------------------------------------------------------
  // 最终状态汇总验证
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Final: 最终状态汇总验证 ===\n";
  {
    // 遍历 server 和 client, 确认条目数一致
    auto server_dumped = dump_grid_items(server);
    auto client_dumped = dump_grid_items(client);
    CASE_EXPECT_EQ(server_dumped.size(), client_dumped.size());

    // 服务器应不为空
    CASE_EXPECT_FALSE(server.is_empty());
    CASE_EXPECT_FALSE(client.is_empty());

    // 占格标记最终一致性
    const auto& sf = server.get_occupy_grid_flag();
    const auto& cf = client.get_occupy_grid_flag();
    for (size_t r = 0; r < sf.row_count(); ++r) {
      for (size_t c = 0; c < sf.column_count(); ++c) {
        CASE_EXPECT_EQ(sf.is_occupied(static_cast<int32_t>(c), static_cast<int32_t>(r)),
                       cf.is_occupied(static_cast<int32_t>(c), static_cast<int32_t>(r)));
      }
    }

    verify_grid_dump(server);
    verify_grid_dump(client);
  }

  CASE_MSG_INFO() << "=== 用户游玩模拟完成 ===\n";
}

// ============================================================
// replace 单元测试
// ============================================================

CASE_TEST(ItemGridAlgorithm, replace) {
  auto server_ptr = atfw::util::memory::make_strong_rc<ServerTestItemGridAlgorithm>();
  auto& server = *server_ptr;
  init_server_grid(server);
  server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto client_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& client = *client_ptr;
  init_test_grid(client);
  client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto config = make_test_config_group();

  // ---- 准备: 通过 add 放入旧数据 (占格x2/装备GUID) ----
  auto item_a = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
  auto item_b = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
  auto equip = make_equip_item(777, 2, 0);
  ItemGridAddRequest add_reqs;
  *add_reqs.Add() = item_a;
  *add_reqs.Add() = item_b;
  *add_reqs.Add() = equip;
  auto result = server.check_add(config, std::move(add_reqs));
  server.add(result);
  sync_and_verify(server, client, config, "replace 前置 add");

  uint64_t old_eid_a = 0;
  uint64_t old_eid_b = 0;
  uint64_t old_eid_equip = 0;

  {
    // 记录旧 entry_id
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos_a;
    gpos_a.mutable_user_inventory()->set_x(0);
    gpos_a.mutable_user_inventory()->set_y(0);
    auto old_a = server.get(gpos_a);
    old_eid_a = old_a ? old_a->entry_id() : 0;

    PROJECT_NAMESPACE_ID::DItemGridPosition gpos_b;
    gpos_b.mutable_user_inventory()->set_x(1);
    gpos_b.mutable_user_inventory()->set_y(0);
    auto old_b = server.get(gpos_b);
    old_eid_b = old_b ? old_b->entry_id() : 0;

    auto old_equip = server.get_by_guid(777);
    old_eid_equip = old_equip ? old_equip->entry_id() : 0;
  }

  CASE_EXPECT_GT(old_eid_a, static_cast<uint64_t>(0));
  CASE_EXPECT_GT(old_eid_b, static_cast<uint64_t>(0));
  CASE_EXPECT_GT(old_eid_equip, static_cast<uint64_t>(0));

  uint64_t next_id_before = server.peek_next_entry_id();

  // ---- 整体替换: 换成全新列表 (数量/位置/GUID 全变) ----
  auto new_item = make_grid_item(kItemTypeId_1x1, 7, 3, 3);
  auto new_equip = make_equip_item(888, 5, 0);
  ItemGridReplaceRequest rep_reqs;
  *rep_reqs.Add() = new_item;
  *rep_reqs.Add() = new_equip;
  ItemGridReplaceRequest restore_reqs = rep_reqs;
  auto rep_checked = server.check_replace(config, std::move(rep_reqs));
  CASE_EXPECT_EQ(rep_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  server.replace(rep_checked);
  sync_and_verify(server, client, config, "replace 后同步");

  // 旧条目应已删除 (find_entry_by_id 找不到)
  verify_not_find_entry_by_id(server, old_eid_a);
  verify_not_find_entry_by_id(server, old_eid_b);
  verify_not_find_entry_by_id(server, old_eid_equip);
  // 旧 guid 也应被移除
  CASE_EXPECT_TRUE(server.get_by_guid(777) == nullptr);

  // 新条目应存在且数据正确
  PROJECT_NAMESPACE_ID::DItemGridPosition gpos_new;
  gpos_new.mutable_user_inventory()->set_x(3);
  gpos_new.mutable_user_inventory()->set_y(3);
  uint64_t new_entry_id = 0;
  {
    auto new_entry = server.get(gpos_new);
    CASE_EXPECT_TRUE(new_entry != nullptr);
    if (new_entry) {
      CASE_EXPECT_EQ(new_entry->item_instance().item_basic().type_id(), kItemTypeId_1x1);
      CASE_EXPECT_EQ(new_entry->item_instance().item_basic().count(), 7);
      // 新条目不应复用旧 entry_id
      CASE_EXPECT_NE(new_entry->entry_id(), old_eid_a);
      new_entry_id = new_entry->entry_id();
    }
  }

  auto new_equip_entry = server.get_by_guid(888);
  CASE_EXPECT_TRUE(new_equip_entry != nullptr);
  if (new_equip_entry) {
    CASE_EXPECT_EQ(new_equip_entry->item_instance().item_basic().type_id(), kEquipmentTypeId);
    CASE_EXPECT_EQ(new_equip_entry->item_instance().item_basic().count(), 1);
  }

  // entry_id 不重置, 单调递增 (replace 创建了新条目)
  CASE_EXPECT_GE(server.peek_next_entry_id(), next_id_before);
  CASE_EXPECT_GT(server.peek_next_entry_id(), next_id_before);

  // ---- replace 空列表 → 清空 Grid ----
  ItemGridReplaceRequest empty_reqs;
  auto empty_checked = server.check_replace(config, std::move(empty_reqs));
  CASE_EXPECT_EQ(empty_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  server.replace(empty_checked);
  CASE_EXPECT_TRUE(server.is_empty());
  if (new_entry_id != 0) {
    verify_not_find_entry_by_id(server, new_entry_id);
  }
  CASE_EXPECT_TRUE(server.get_by_guid(888) == nullptr);
  sync_and_verify(server, client, config, "replace 清空后同步");

  // ---- check_replace 失败用例: 只检查不修改, Grid 数据保持基准不变 ----
  // 先放回一组数据作为基准
  auto checkede = server.check_replace(config, std::move(restore_reqs));
  server.replace(checkede);
  CASE_EXPECT_TRUE(server.get_by_guid(888) != nullptr);

  // 1. 空实例
  {
    PROJECT_NAMESPACE_ID::DItemInstance invalid_item;
    ItemGridReplaceRequest reqs;
    *reqs.Add() = new_item;
    *reqs.Add() = invalid_item;
    auto checked = server.check_replace(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    CASE_EXPECT_EQ(checked.result.failed_index, 1);
  }
  // 2. 重复 GUID
  {
    auto e1 = make_equip_item(999, 0, 0);
    auto e2 = make_equip_item(999, 1, 0);
    ItemGridReplaceRequest reqs;
    *reqs.Add() = e1;
    *reqs.Add() = e2;
    auto checked = server.check_replace(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID);
  }
  // 3. 同位置不同类型 → 占用
  {
    auto i1 = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    auto i2 = make_grid_item(kItemTypeId_2x2, 1, 0, 0);
    ItemGridReplaceRequest reqs;
    *reqs.Add() = i1;
    *reqs.Add() = i2;
    auto checked = server.check_replace(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED);
  }
  // 4. 数量超过堆叠上限
  {
    auto i1 = make_grid_item(kItemTypeId_1x1, 100, 0, 0);
    ItemGridReplaceRequest reqs;
    *reqs.Add() = i1;
    auto checked = server.check_replace(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
  }
  // 5. 越界
  {
    auto i1 = make_grid_item(kItemTypeId_2x2, 1, 9, 9);
    ItemGridReplaceRequest reqs;
    *reqs.Add() = i1;
    auto checked = server.check_replace(config, std::move(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OUT_OF_RANGE);
  }

  // 所有失败用例后 Grid 数据保持基准不变
  CASE_EXPECT_TRUE(server.get_by_guid(888) != nullptr);
  verify_grid_dump(server);
}

// ============================================================
// ItemGridContainer::replace 单元测试
// ============================================================

CASE_TEST(ItemGridContainer, replace) {
  DualGridContainer container;
  auto config = make_test_config_group();

  // 前置 add: inventory 2 件, backpack 1 件
  auto i1 = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
  auto i2 = make_grid_item(kItemTypeId_1x1, 5, 1, 0);
  ItemGridAddRequest add_reqs;
  *add_reqs.Add() = i1;
  *add_reqs.Add() = i2;
  auto checked = container.check_add(config, std::move(add_reqs));
  container.add(checked);

  auto b1 = make_grid_item(kItemTypeId_1x1, 3, 0, 0);
  b1.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_x(0);
  b1.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_y(0);
  ItemGridAddRequest backpack_add_reqs;
  *backpack_add_reqs.Add() = b1;
  auto backpack_checked = container.check_add(config, std::move(backpack_add_reqs));
  container.add(backpack_checked);

  CASE_EXPECT_EQ(container.inventory_grid->get_item_count(kItemTypeId_1x1), 10);
  CASE_EXPECT_EQ(container.backpack_grid->get_item_count(kItemTypeId_1x1), 3);

  // 整体替换: 两个 Grid 各换一个新条目 (通过位置路由到对应 Grid)
  auto new_inv = make_grid_item(kItemTypeId_1x1, 8, 2, 2);
  auto new_bp = make_grid_item(kItemTypeId_1x1, 9, 1, 1);
  new_bp.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_x(1);
  new_bp.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_y(1);

  ItemGridReplaceRequest rep_reqs;
  *rep_reqs.Add() = new_inv;
  *rep_reqs.Add() = new_bp;
  auto checked_2 = container.check_replace(config, std::move(rep_reqs));
  CASE_EXPECT_EQ(checked_2.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto rep_result = container.replace(checked_2);
  CASE_EXPECT_EQ(rep_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  // 每个 Grid 都只剩自己的新条目
  CASE_EXPECT_EQ(container.inventory_grid->get_item_count(kItemTypeId_1x1), 8);
  CASE_EXPECT_EQ(container.backpack_grid->get_item_count(kItemTypeId_1x1), 9);

  PROJECT_NAMESPACE_ID::DItemGridPosition gpos_inv;
  gpos_inv.mutable_user_inventory()->set_x(2);
  gpos_inv.mutable_user_inventory()->set_y(2);
  auto inv_entry = container.inventory_grid->get(gpos_inv);
  CASE_EXPECT_TRUE(inv_entry != nullptr);
  if (inv_entry) {
    CASE_EXPECT_EQ(inv_entry->item_instance().item_basic().count(), 8);
  }

  PROJECT_NAMESPACE_ID::DItemGridPosition gpos_bp;
  gpos_bp.mutable_character_inventory()->set_x(1);
  gpos_bp.mutable_character_inventory()->set_y(1);
  auto bp_entry = container.backpack_grid->get(gpos_bp);
  CASE_EXPECT_TRUE(bp_entry != nullptr);
  if (bp_entry) {
    CASE_EXPECT_EQ(bp_entry->item_instance().item_basic().count(), 9);
  }

  // 跨 Grid 请求中任一 Grid 校验失败 → 整体 check 失败, 不执行任何 replace
  {
    auto bad_inv = make_grid_item(kItemTypeId_2x2, 1, 9, 9);  // 越界
    auto ok_bp = make_grid_item(kItemTypeId_1x1, 2, 0, 0);
    ok_bp.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_x(0);
    ok_bp.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_y(0);
    ItemGridReplaceRequest bad_reqs;
    *bad_reqs.Add() = bad_inv;
    *bad_reqs.Add() = ok_bp;
    auto bad_checked = container.check_replace(config, std::move(bad_reqs));
    CASE_EXPECT_NE(bad_checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(container.inventory_grid->get_item_count(kItemTypeId_1x1), 8);
  CASE_EXPECT_EQ(container.backpack_grid->get_item_count(kItemTypeId_1x1), 9);
}

// ============================================================
// find_positions_for_basics 单元测试
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_for_basics) {
  using namespace item_algorithm;

  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  // ---- 辅助：构造 DItemBasic ----
  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };

  // 带首选格子位置的 DItemBasic (inventory 坐标)
  auto make_basic_at = [](int32_t type_id, int64_t count, int32_t x, int32_t y) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    b.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
    b.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
    return b;
  };

  // 读取 inventory 坐标分量
  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  // 辅助：调用新签名, success/failed 为输出参数
  auto call_find = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed,
                       const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& ignore = {}) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    for (const auto& b : ignore) {
      *ignore_field.Add() = b;
    }
    return grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed);
  };

  // 从 success_item 中读取位置
  auto pos_of = [](const PROJECT_NAMESPACE_ID::DItemBasic& b) -> const PROJECT_NAMESPACE_ID::DItemGridPosition& {
    return b.position().grid_position();
  };

  // ============================================================
  // Case 1: 非占格道具 -> 输出空 DItemGridPosition
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 非占格道具输出空位置\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kNoPosition, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kCoinTypeId, 10),
        make_basic(kVirtualTypeId, 5),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(2u, success.size());
    CASE_EXPECT_TRUE(failed.empty());
    CASE_EXPECT_TRUE(pos_of(success[0]).has_virtual_inventory());
    CASE_EXPECT_TRUE(pos_of(success[1]).has_virtual_inventory());
  }

  // ============================================================
  // Case 2: 空网格批量 1x1 - 游标递进，分配连续不重复格子
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 空网格批量分配游标递进\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 3, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(3u, success.size());
    CASE_EXPECT_TRUE(failed.empty());

    // 游标优化：第一件在 (0,0)，后续递增
    CASE_EXPECT_EQ(0, get_x(pos_of(success[0])));
    CASE_EXPECT_EQ(0, get_y(pos_of(success[0])));
    CASE_EXPECT_EQ(1, get_x(pos_of(success[1])));
    CASE_EXPECT_EQ(0, get_y(pos_of(success[1])));
    CASE_EXPECT_EQ(2, get_x(pos_of(success[2])));
    CASE_EXPECT_EQ(0, get_y(pos_of(success[2])));

    // 三件物品不重复
    std::set<std::pair<int32_t, int32_t>> pos_set;
    for (const auto& s : success) {
      pos_set.insert({get_x(pos_of(s)), get_y(pos_of(s))});
    }
    CASE_EXPECT_EQ(3u, pos_set.size());
  }

  // ============================================================
  // Case 5: 堆叠到已有条目（accumulation_limit > 1）
  // ============================================================
  CASE_MSG_INFO() << "Case 5: 堆叠到已有条目\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    // 先 load 一件到 (x=0, y=0), count=50，堆叠上限99
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(50);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_1x1, 10),  // 剩余 49，可堆叠
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    // 定向到已有 entry (x=0, y=0)
    CASE_EXPECT_EQ(0, get_x(pos_of(success[0])));
    CASE_EXPECT_EQ(0, get_y(pos_of(success[0])));
  }

  // ============================================================
  // Case 6: 背包全满
  // ============================================================
  CASE_MSG_INFO() << "Case 6: 背包全满\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);  // 堆叠上限=1，不可堆叠

    for (int32_t y = 0; y < 2; ++y) {
      for (int32_t x = 0; x < 2; ++x) {
        PROJECT_NAMESPACE_ID::DItemInstance inst;
        inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
        inst.mutable_item_basic()->set_count(1);
        inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
        inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
        CASE_EXPECT_TRUE(grid.load(config, inst));
      }
    }

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 1)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    // 背包已满: 调用成功, 道具放入 failed_item
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_TRUE(success.empty());
    CASE_EXPECT_EQ(1u, failed.size());
  }

  // ============================================================
  // Case 7: 2x2 物品批量分配，不重叠
  // ============================================================
  CASE_MSG_INFO() << "Case 7: 2x2 物品批量分配\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_2x2, 1),
        make_basic(kItemTypeId_2x2, 1),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(2u, success.size());
    CASE_EXPECT_TRUE(failed.empty());

    // 每个位置在范围内（2x2 起点 x in [0,2], y in [0,2]）
    for (const auto& s : success) {
      CASE_EXPECT_TRUE(get_x(pos_of(s)) >= 0 && get_x(pos_of(s)) + 2 <= 4);
      CASE_EXPECT_TRUE(get_y(pos_of(s)) >= 0 && get_y(pos_of(s)) + 2 <= 4);
    }
    // 两件起点不同（不重叠）
    bool same_pos = (get_x(pos_of(success[0])) == get_x(pos_of(success[1])) &&
                     get_y(pos_of(success[0])) == get_y(pos_of(success[1])));
    CASE_EXPECT_FALSE(same_pos);
  }

  // ============================================================
  // Case 8: 混合批次 1x1 与 2x2 - 批次内互不干扰
  // ============================================================
  CASE_MSG_INFO() << "Case 8: 混合批次 1x1 与 2x2\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_2x2, 1),  // 先放 2x2
        make_basic(kItemTypeId_1x1, 1),  // 再放 1x1，应躲开 2x2 区域
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(2u, success.size());
    CASE_EXPECT_TRUE(failed.empty());

    // 输出顺序不保证, 遍历找 2x2 和 1x1
    int32_t x2 = -1, y2 = -1, x1 = -1, y1 = -1;
    for (const auto& s : success) {
      if (s.type_id() == kItemTypeId_2x2) {
        x2 = get_x(pos_of(s));
        y2 = get_y(pos_of(s));
      } else if (s.type_id() == kItemTypeId_1x1) {
        x1 = get_x(pos_of(s));
        y1 = get_y(pos_of(s));
      }
    }
    CASE_EXPECT_TRUE(x2 >= 0 && x1 >= 0);

    // 1x1 不能落在 2x2 所占的区域内
    bool overlap = (x1 >= x2 && x1 < x2 + 2 && y1 >= y2 && y1 < y2 + 2);
    CASE_EXPECT_FALSE(overlap);
  }

  // ============================================================
  // Case 9: 游标优化 - 前三列满，仅第4列(x=3)空，批量放4件
  // ============================================================
  CASE_MSG_INFO() << "Case 9: 游标优化接近满格验证\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);  // 不可堆叠

    // 填满 x=0,1,2，留 x=3 空闲
    for (int32_t y = 0; y < 4; ++y) {
      for (int32_t x = 0; x < 3; ++x) {
        PROJECT_NAMESPACE_ID::DItemInstance inst;
        inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
        inst.mutable_item_basic()->set_count(1);
        inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
        inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
        CASE_EXPECT_TRUE(grid.load(config, inst));
      }
    }

    // 剩余 4 个空格，批量请求 4 件
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics(4, make_basic(kItemTypeId_1x1, 1));
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(4u, success.size());
    CASE_EXPECT_TRUE(failed.empty());

    std::set<std::pair<int32_t, int32_t>> pos_set;
    for (const auto& s : success) {
      CASE_EXPECT_EQ(3, get_x(pos_of(s)));  // 必须在 x=3 列
      CASE_EXPECT_TRUE(get_y(pos_of(s)) >= 0 && get_y(pos_of(s)) < 4);
      pos_set.insert({get_x(pos_of(s)), get_y(pos_of(s))});
    }
    CASE_EXPECT_EQ(4u, pos_set.size());  // 无重复

    // find_positions_for_basics 是只读规划，不修改格子。
    // 需要实际 load 这 4 件到格子中，再验证满格。
    for (const auto& s : success) {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      *inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = pos_of(s);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 格子现在全满，第 5 件放不下 → 放入 failed_item
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> one_more = {make_basic(kItemTypeId_1x1, 1)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success2;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed2;
    bool ok2 = call_find(grid, one_more, success2, failed2);
    CASE_EXPECT_TRUE(ok2);
    CASE_EXPECT_TRUE(success2.empty());
    CASE_EXPECT_EQ(1u, failed2.size());
  }

  // ============================================================
  // Case 11: on_find_position_for_infinite 钩子被调用
  //          默认实现返回 false -> 道具放入 failed_item
  // ============================================================
  CASE_MSG_INFO() << "Case 11: non-care 走钩子，默认钩子返回 false\n";
  {
    // 用 kCharacterEquipment 初始化
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kInfiniteGrid, 1, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment,
              0);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 1)};
    // 默认 on_find_position_for_infinite 返回 false -> 道具放入 failed_item
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_TRUE(success.empty());
    CASE_EXPECT_EQ(1u, failed.size());
  }

  // ============================================================
  // Case 12: on_check_add 拦截格子（子类覆盖拒绝 x=0 的位置）
  //          扫描应跳过 x=0，最终分配到 x>=1 的格子
  // ============================================================
  CASE_MSG_INFO() << "Case 12: on_check_add 拦截特定格子\n";
  {
    // 子类：拒绝 x=0 的所有 inventory 位置
    class RejectFirstColGrid : public TestItemGridAlgorithm {
     protected:
      int32_t on_check_add(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                           const PROJECT_NAMESPACE_ID::DItemInstance& request) const override {
        if (request.item_basic().position().grid_position().user_inventory().x() == 0) {
          return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // 拒绝 x=0
        }
        return TestItemGridAlgorithm::on_check_add(config_group, request);
      }
    };

    auto grid_ptr = atfw::util::memory::make_strong_rc<RejectFirstColGrid>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 1)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    // 结果必须不在 x=0 列
    CASE_EXPECT_NE(0, get_x(pos_of(success[0])));
    CASE_EXPECT_TRUE(get_x(pos_of(success[0])) >= 1 && get_x(pos_of(success[0])) < 4);
    CASE_EXPECT_TRUE(get_y(pos_of(success[0])) >= 0 && get_y(pos_of(success[0])) < 4);
  }

  CASE_MSG_INFO() << "=== find_positions_for_basics 测试完成 ===\n";
}

// ============================================================
// find_positions_for_basics — ignore_item 与拆堆放置
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_for_basics_ignore_item) {
  using namespace item_algorithm;
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };
  auto make_basic_at = [](int32_t type_id, int64_t count, int32_t x, int32_t y) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    b.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
    b.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
    return b;
  };
  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  auto call_find = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed,
                       const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& ignore) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    for (const auto& b : ignore) {
      *ignore_field.Add() = b;
    }
    return grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed);
  };

  // ============================================================
  // Case 1: 消耗 A×1 兑换 A×2, 堆叠上限 99
  //         原位 (0,0) 已有 A×1, ignore_item 消耗 1 个 → 空出容量,
  //         兑换的 2 个都堆回原位 (容量 99 足够, 无需找新位置)。
  // ============================================================
  CASE_MSG_INFO() << "Case 1: ignore_item 消耗后堆回原位\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    // 原位 (0,0) 已有 A×1
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 消耗 A×1 (原位), 兑换 A×2
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 2)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic_at(kItemTypeId_1x1, 1, 0, 0)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_TRUE(ok);
    // 堆叠上限 99, 消耗 1 个空出容量后 2 个都堆回原位
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_TRUE(failed.empty());
    if (success.size() == 1) {
      CASE_EXPECT_EQ(get_x(success[0].position().grid_position()), 0);
      CASE_EXPECT_EQ(get_y(success[0].position().grid_position()), 0);
      CASE_EXPECT_EQ(success[0].count(), 2);
    }
  }

  // ============================================================
  // Case 2: 堆叠上限 1, 消耗 A×1 兑换 A×2 → 必须拆堆到两个位置
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 堆叠上限 1 拆堆放置\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);  // 堆叠上限 1

    // 原位 (0,0) 已有 A×1
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 消耗 A×1 (原位), 兑换 A×2 → 1 个堆回原位, 1 个找新位置
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 2)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic_at(kItemTypeId_1x1, 1, 0, 0)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(2u, success.size());
    CASE_EXPECT_TRUE(failed.empty());

    bool has_origin = false;
    bool has_new = false;
    for (const auto& s : success) {
      if (get_x(s.position().grid_position()) == 0 && get_y(s.position().grid_position()) == 0) {
        has_origin = true;
      } else {
        has_new = true;
      }
    }
    CASE_EXPECT_TRUE(has_origin);
    CASE_EXPECT_TRUE(has_new);
  }

  // ============================================================
  // Case 3: ignore_item 数量不足 → 整体失败
  // ============================================================
  CASE_MSG_INFO() << "Case 3: ignore_item 数量不足整体失败\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    // 原位 (0,0) 只有 A×1
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 消耗 A×2 (超过持有 1) → 整体失败
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic_at(kItemTypeId_1x1, 2, 0, 0)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_FALSE(ok);
    CASE_EXPECT_TRUE(success.empty());
  }

  // ============================================================
  // Case 4: ignore_item 不存在 → 整体失败
  // ============================================================
  CASE_MSG_INFO() << "Case 4: ignore_item 不存在整体失败\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic_at(kItemTypeId_1x1, 1, 3, 3)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_FALSE(ok);
    CASE_EXPECT_TRUE(success.empty());
  }

  // ============================================================
  // Case 5: 无限格子不占格模式传入 ignore_item → 整体失败
  // ============================================================
  CASE_MSG_INFO() << "Case 5: 无限格子不占格模式不支持 ignore_item\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kInfiniteGrid, 1, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment,
              0);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kEquipmentTypeId, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic(kEquipmentTypeId, 1)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_FALSE(ok);
    CASE_EXPECT_TRUE(success.empty());
  }

  // ============================================================
  // Case 6: 无位置模式传入 ignore_item → 整体失败
  // ============================================================
  CASE_MSG_INFO() << "Case 6: 无位置模式不支持 ignore_item\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kCoinTypeId, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic(kCoinTypeId, 1)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_FALSE(ok);
    CASE_EXPECT_TRUE(success.empty());
  }
}

// ============================================================
// find_positions_for_basics — Review 问题回归
//
// 1. 策略2 批次内累计已规划数量: 同一批多个同类型可堆叠 basic 规划到同一位置,
//    合计不能超过 accumulation_limit (否则 check_add 会以 STACK_OVERFLOW 拒绝)。
// 2. accumulation_limit <= 0 收敛为 1: 策略1/3 不能把缺省值 0 当作无上限。
// 3. ignore_item 腾位: 被整体消耗的格子应从 reserved 释放, 允许新道具落回原位
//    (GUID 道具 / 消耗位与新道具类型不同场景)。
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_for_basics_review_issues) {
  using namespace item_algorithm;
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };
  auto make_basic_at = [](int32_t type_id, int64_t count, int32_t x, int32_t y) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    b.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
    b.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
    return b;
  };
  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  auto call_find = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed,
                       const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& ignore = {}) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    for (const auto& b : ignore) {
      *ignore_field.Add() = b;
    }
    return grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed);
  };

  // ============================================================
  // Case 1: 策略2 批次内累计 — 已有 A×59 (limit 99), 传入 [{A,30},{A,30}]
  //         两条各规划 30 到同一位置会超限, 第二条只能吸收 10, 剩余 20 进 failed。
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 策略2 批次内累计已规划数量\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 1, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    // 原位 (0,0) 已有 A×59
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(59);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 传入 [{A,30},{A,30}]
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 30),
                                                            make_basic(kItemTypeId_1x1, 30)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);

    // 规划到 (0,0) 的总量不能超过 99 (已有 59 + 规划 ≤ 99)
    int64_t planned_at_origin = 0;
    for (const auto& s : success) {
      if (get_x(s.position().grid_position()) == 0 && get_y(s.position().grid_position()) == 0) {
        planned_at_origin += s.count();
      }
    }
    CASE_EXPECT_LE(59 + planned_at_origin, 99);
    // 放不下的 20 进 failed_item
    CASE_EXPECT_EQ(1u, failed.size());
    if (failed.size() == 1) {
      CASE_EXPECT_EQ(failed[0].count(), 20);
    }
  }

  // ============================================================
  // Case 2: accumulation_limit <= 0 视为无限堆叠 (INT32_MAX)
  //         配置缺省 limit=0, 策略1/3 应视为无限堆叠, 全部堆到同一位置。
  // ============================================================
  CASE_MSG_INFO() << "Case 2: accumulation_limit<=0 视为无限堆叠\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 0, 1, 1);  // limit=0 (缺省)

    // 传入 A×2, limit=0 视为无限堆叠 → 2 个都堆到同一位置 (count=2)
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 2)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_TRUE(failed.empty());
    if (success.size() == 1) {
      CASE_EXPECT_EQ(success[0].count(), 2);  // 无限堆叠, 全部堆到同一位置
    }
  }

  // ============================================================
  // Case 3: ignore_item 腾位 — 满背包消耗 GUID 装备换新 GUID 装备
  //         被整体消耗的格子应从 reserved 释放, 新装备落回原位。
  // ============================================================
  CASE_MSG_INFO() << "Case 3: ignore_item 腾位 (GUID 装备)\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    // (0,0) 放 GUID 装备 1001, 其余 3 格放 1x1 道具 → 满背包
    CASE_EXPECT_TRUE(grid.load(config, make_equip_item(1001, 0, 0)));
    CASE_EXPECT_TRUE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 1, 0)));
    CASE_EXPECT_TRUE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 0, 1)));
    CASE_EXPECT_TRUE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 1, 1)));

    // 消耗装备 1001 (原位), 兑换新装备 2001
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics;
    basics.push_back(make_equip_sub_by_guid(2001));  // 新装备 (GUID 2001)
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore;
    ignore.push_back(make_equip_sub_by_guid(1001));  // 消耗旧装备 (GUID 1001)
    // 新装备指向 (0,0) (旧装备原位)
    basics[0].mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
    basics[0].mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);

    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_TRUE(ok);
    // 新装备应落回原位 (0,0), 而不是进 failed_item
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_TRUE(failed.empty());
    if (success.size() == 1) {
      CASE_EXPECT_EQ(get_x(success[0].position().grid_position()), 0);
      CASE_EXPECT_EQ(get_y(success[0].position().grid_position()), 0);
    }
  }

  // ============================================================
  // Case 4: 策略2 + 策略3 混合预订 — 被 ignore_item 整体消耗的格子
  //         策略2 堆回后必须标记 reserved, 防止策略3 再次预订同一格。
  //         1x2 背包, limit=99, (1,0) 已有 A×50, ignore=[A×50@(1,0)],
  //         basics=[{A,250}] → (1,0) 恰好 99, 剩余 52 进 failed_item。
  // ============================================================
  CASE_MSG_INFO() << "Case 4: 策略2+策略3 混合预订 (ignore_item 腾位)\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 1, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    // (1,0) 已有 A×50
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(50);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(1);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 消耗 A×50@(1,0) (整体消耗), 兑换 A×250
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 250)};
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> ignore = {make_basic_at(kItemTypeId_1x1, 50, 1, 0)};
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed, ignore);
    CASE_EXPECT_TRUE(ok);

    // 统计每个位置规划的总量
    int64_t planned_at_00 = 0;
    int64_t planned_at_10 = 0;
    for (const auto& s : success) {
      if (get_x(s.position().grid_position()) == 0 && get_y(s.position().grid_position()) == 0) {
        planned_at_00 += s.count();
      } else if (get_x(s.position().grid_position()) == 1 && get_y(s.position().grid_position()) == 0) {
        planned_at_10 += s.count();
      }
    }
    // (1,0) 被整体消耗后, 策略2 堆回 + 策略3 扫描不能超过 limit=99
    CASE_EXPECT_LE(planned_at_10, 99);
    // (0,0) 最多 99
    CASE_EXPECT_LE(planned_at_00, 99);
    // 剩余 250 - 99 - 99 = 52 进 failed_item
    CASE_EXPECT_EQ(1u, failed.size());
    if (failed.size() == 1) {
      CASE_EXPECT_EQ(failed[0].count(), 52);
    }
  }
}

// ============================================================
// find_positions_for_basics — 两次搜索位置 (多背包依次塞入)
//
// 第一次调用只放入部分道具, 调用方把未放入成功的道具放入 failed_item,
// 再把 failed_item 作为下一次 basics 调用 (模拟依次塞入多个背包)。
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_for_basics_two_pass) {
  using namespace item_algorithm;
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };
  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  auto call_find = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    return grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed);
  };

  // ============================================================
  // Case 1: 第一次只放得下 2 件 (2x2 背包), 剩余 2 件放入 failed_item,
  //         第二次用 failed_item 作为 basics 在另一个背包放置。
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 两次搜索位置 (多背包依次塞入)\n";
  {
    // 第一个背包: 2x2, 堆叠上限 1, 只能放 2 件
    auto grid1_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid1 = *grid1_ptr;
    register_test_log_handler(grid1);
    grid1.init(ItemGridAlgorithmMode::kFiniteGrid, 2, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid1.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    // 第二个背包: 2x2, 堆叠上限 1, 也能放 2 件
    auto grid2_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid2 = *grid2_ptr;
    register_test_log_handler(grid2);
    grid2.init(ItemGridAlgorithmMode::kFiniteGrid, 2, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid2.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    // 4 件 1x1, 堆叠上限 1
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
    };

    // 第一次: 背包1 只放得下 2 件, 剩余 2 件由函数放入 failed_item
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success1;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed1;
    bool ok1 = call_find(grid1, basics, success1, failed1);
    CASE_EXPECT_TRUE(ok1);
    CASE_EXPECT_EQ(2u, success1.size());
    CASE_EXPECT_EQ(2u, failed1.size());

    // 第二次: 用 failed_item 作为 basics 在背包2 放置
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> remaining;
    for (const auto& b : failed1) {
      remaining.push_back(b);
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success2;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed2;
    bool ok2 = call_find(grid2, remaining, success2, failed2);
    CASE_EXPECT_TRUE(ok2);
    CASE_EXPECT_EQ(2u, success2.size());
    CASE_EXPECT_TRUE(failed2.empty());

    // 两个背包合计放入 4 件
    CASE_EXPECT_EQ(success1.size() + success2.size(), static_cast<size_t>(4));
  }
}

// ============================================================
// find_positions_for_instances — DItemInstance 输入版本
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_for_instances) {
  using namespace item_algorithm;
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_instance = [](int32_t type_id, int64_t count, int32_t x, int32_t y) {
    PROJECT_NAMESPACE_ID::DItemInstance inst;
    auto* basic = inst.mutable_item_basic();
    basic->set_type_id(type_id);
    basic->set_count(count);
    basic->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
    basic->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
    return inst;
  };
  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  auto call_find = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemInstance>& items,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed,
                       const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& ignore = {}) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> items_field;
    for (const auto& v : items) {
      *items_field.Add() = v;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    for (const auto& v : ignore) {
      *ignore_field.Add() = v;
    }
    return grid.find_positions_for_instances(config, items_field, ignore_field, success, failed);
  };

  // ============================================================
  // Case 1: 占格道具找位置, 输出 DItemInstance (带位置)
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 占格道具找位置\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemInstance> items = {
        make_instance(kItemTypeId_1x1, 1, 0, 0),
        make_instance(kItemTypeId_1x1, 1, 0, 0),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed;
    bool ok = call_find(grid, items, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(2u, success.size());
    CASE_EXPECT_TRUE(failed.empty());
    // 两个位置不同
    bool same = (get_x(success[0].item_basic().position().grid_position()) ==
                     get_x(success[1].item_basic().position().grid_position()) &&
                 get_y(success[0].item_basic().position().grid_position()) ==
                     get_y(success[1].item_basic().position().grid_position()));
    CASE_EXPECT_FALSE(same);
  }

  // ============================================================
  // Case 2: 非占格道具输出空位置
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 非占格道具输出空位置\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);

    std::vector<PROJECT_NAMESPACE_ID::DItemInstance> items = {
        make_instance(kCoinTypeId, 10, 0, 0),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed;
    bool ok = call_find(grid, items, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_TRUE(failed.empty());
    CASE_EXPECT_FALSE(success[0].item_basic().position().grid_position().has_virtual_inventory());
  }

  // ============================================================
  // Case 3: 背包放不下 → 放入 failed_item (DItemInstance)
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 背包放不下放入 failed_item\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 1, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemInstance> items = {
        make_instance(kItemTypeId_1x1, 1, 0, 0),
        make_instance(kItemTypeId_1x1, 1, 0, 0),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed;
    bool ok = call_find(grid, items, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_EQ(1u, failed.size());
    CASE_EXPECT_EQ(failed[0].item_basic().type_id(), kItemTypeId_1x1);
  }
}

// ============================================================
// find_positions — 输出物品填充 container_guid
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_fill_container_guid) {
  using namespace item_algorithm;
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();
  constexpr int64_t kContainerGuid = 10001;

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };

  auto call_find_basics = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                              google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                              google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    return grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed);
  };

  // ============================================================
  // Case 1: 无位置模式 (kNoPosition) — 输出填充 container_guid
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 无位置模式输出填充 container_guid\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory,
              kContainerGuid);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kCoinTypeId, 10),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find_basics(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_EQ(kContainerGuid, success[0].position().container_guid());
  }

  // ============================================================
  // Case 2: 堆叠到已有条目 — 输出填充 container_guid
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 堆叠到已有条目输出填充 container_guid\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory,
              kContainerGuid);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    // 先 load 一件到 (0,0), count=50, 堆叠上限99
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(50);
      inst.mutable_item_basic()->mutable_position()->set_container_guid(kContainerGuid);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(0);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(0);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_1x1, 10),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find_basics(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_EQ(kContainerGuid, success[0].position().container_guid());
  }

  // ============================================================
  // Case 3: 位图扫描 (空网格, 不可堆叠) — 输出填充 container_guid
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 位图扫描输出填充 container_guid\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory,
              kContainerGuid);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);  // 不可堆叠, 走位图扫描

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_1x1, 1),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find_basics(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_EQ(kContainerGuid, success[0].position().container_guid());
  }

  // ============================================================
  // Case 4: find_positions_for_instances 位图扫描 — 输出填充 container_guid
  // ============================================================
  CASE_MSG_INFO() << "Case 4: find_positions_for_instances 输出填充 container_guid\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory,
              kContainerGuid);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    PROJECT_NAMESPACE_ID::DItemInstance inst;
    inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
    inst.mutable_item_basic()->set_count(1);
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> items_field;
    *items_field.Add() = inst;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed;
    bool ok = grid.find_positions_for_instances(config, items_field, ignore_field, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_EQ(kContainerGuid, success[0].item_basic().position().container_guid());
  }
}

// ============================================================
// find_positions — 背包仅剩不同大小位置时, 从大到小正好放入
// ============================================================

CASE_TEST(ItemGridAlgorithm, find_positions_fit_various_sizes) {
  using namespace item_algorithm;
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };

  auto call_find = [&](TestItemGridAlgorithm& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    return grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed);
  };

  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  // ============================================================
  // Case 1: 4x4 背包仅剩 2x2 / 2x1 / 1x1 三个区域, 从大到小正好放入
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 不同大小位置从大到小正好放入\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(ItemGridAlgorithmMode::kFiniteGrid, 4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);  // 宽2 高2
    grid.register_position_cfg(kItemTypeId_2x1, 1, 1, 2);  // 宽2 高1
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);  // 宽1 高1

    // 占位后仅剩:
    //   行0: (0,0)(1,0)(2,0)(3,0) 空闲
    //   行1: (0,1)(1,1) 空闲, (2,1)(3,1) 占位
    //   行2: 全部占位
    //   行3: (0,3)(1,3)(2,3) 占位, (3,3) 空闲
    // 空闲区域: (0,0)-(1,1) 2x2, (2,0)-(3,0) 2x1, (3,3) 1x1
    auto load_occupy = [&](int32_t x, int32_t y) {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(x);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(y);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    };
    load_occupy(2, 1);
    load_occupy(3, 1);
    for (int32_t x = 0; x < 4; ++x) {
      load_occupy(x, 2);
    }
    load_occupy(0, 3);
    load_occupy(1, 3);
    load_occupy(2, 3);

    // 待放入: 2x2, 2x1, 1x1 (排序后从大到小: 2x2 -> 2x1 -> 1x1)
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_2x2, 1),
        make_basic(kItemTypeId_2x1, 1),
        make_basic(kItemTypeId_1x1, 1),
    };
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
    bool ok = call_find(grid, basics, success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(3u, success.size());
    CASE_EXPECT_TRUE(failed.empty());

    // 验证每个物品放入对应大小的空闲区域
    int32_t x2 = -1, y2 = -1, x21 = -1, y21 = -1, x1 = -1, y1 = -1;
    for (const auto& s : success) {
      if (s.type_id() == kItemTypeId_2x2) {
        x2 = get_x(s.position().grid_position());
        y2 = get_y(s.position().grid_position());
      } else if (s.type_id() == kItemTypeId_2x1) {
        x21 = get_x(s.position().grid_position());
        y21 = get_y(s.position().grid_position());
      } else if (s.type_id() == kItemTypeId_1x1) {
        x1 = get_x(s.position().grid_position());
        y1 = get_y(s.position().grid_position());
      }
    }
    // 2x2 放入 (0,0)
    CASE_EXPECT_EQ(0, x2);
    CASE_EXPECT_EQ(0, y2);
    // 2x1 放入 (2,0)
    CASE_EXPECT_EQ(2, x21);
    CASE_EXPECT_EQ(0, y21);
    // 1x1 放入 (3,3)
    CASE_EXPECT_EQ(3, x1);
    CASE_EXPECT_EQ(3, y1);
  }
}

CASE_TEST(ItemGridAlgorithm, checked_request_validation) {
  constexpr int64_t kPrimaryContainerGuid = 10001;
  constexpr int64_t kSecondaryContainerGuid = 10002;

  auto primary_grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& primary_grid = *primary_grid_ptr;
  init_test_grid(primary_grid, 10, 10, kPrimaryContainerGuid);

  auto secondary_grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& secondary_grid = *secondary_grid_ptr;
  init_test_grid(secondary_grid, 10, 10, kSecondaryContainerGuid);

  auto config = make_test_config_group();

  // 使用错误 Grid 执行已检查请求必须被 container_guid 拒绝，且请求和两个 Grid 均保持未修改。
  auto first_item = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  first_item.mutable_item_basic()->mutable_position()->set_container_guid(kPrimaryContainerGuid);
  ItemGridAddRequest first_requests;
  *first_requests.Add() = first_item;
  auto checked_for_primary = primary_grid.check_add(config, std::move(first_requests));
  CASE_EXPECT_EQ(checked_for_primary.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto wrong_grid_result = secondary_grid.add(checked_for_primary);
  CASE_EXPECT_EQ(wrong_grid_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_FALSE(checked_for_primary.apply);
  CASE_EXPECT_EQ(primary_grid.get_item_count(kItemTypeId_1x1), 0);
  CASE_EXPECT_EQ(secondary_grid.get_item_count(kItemTypeId_1x1), 0);

  auto primary_result = primary_grid.add(checked_for_primary);
  CASE_EXPECT_EQ(primary_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(primary_grid.get_item_count(kItemTypeId_1x1), 1);

  // 第二次 check 会使第一次结果的 operate_id 失效，只有最新结果能够执行。
  auto stale_item = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
  stale_item.mutable_item_basic()->mutable_position()->set_container_guid(kPrimaryContainerGuid);
  ItemGridAddRequest stale_requests;
  *stale_requests.Add() = stale_item;
  auto stale_checked = primary_grid.check_add(config, std::move(stale_requests));
  CASE_EXPECT_EQ(stale_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto latest_item = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  latest_item.mutable_item_basic()->mutable_position()->set_container_guid(kPrimaryContainerGuid);
  ItemGridAddRequest latest_requests;
  *latest_requests.Add() = latest_item;
  auto latest_checked = primary_grid.check_add(config, std::move(latest_requests));
  CASE_EXPECT_EQ(latest_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto stale_result = primary_grid.add(stale_checked);
  CASE_EXPECT_EQ(stale_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_FALSE(stale_checked.apply);
  CASE_EXPECT_EQ(primary_grid.get_item_count(kItemTypeId_1x1), 1);

  auto latest_result = primary_grid.add(latest_checked);
  CASE_EXPECT_EQ(latest_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(primary_grid.get_item_count(kItemTypeId_1x1), 2);
  verify_grid_dump(primary_grid);
}

CASE_TEST(ItemGridAlgorithm, no_position_ungrid_lifecycle) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  CASE_EXPECT_TRUE(grid.get_occupy_grid_flag().empty());

  ItemGridAddRequest add_requests;
  *add_requests.Add() = make_ungrid_item(kCoinTypeId, 10);
  auto add_checked = grid.check_add(config, std::move(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 10);
  verify_item_count_consistency(grid, kCoinTypeId);

  ItemGridSubRequest sub_requests;
  *sub_requests.Add() = make_sub_basic(kCoinTypeId, 4);
  auto sub_checked = grid.check_sub(config, std::move(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.sub(sub_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 6);
  verify_item_count_consistency(grid, kCoinTypeId);

  // check_replace 必须在 clone 后仍保留无位置模式。
  ItemGridReplaceRequest replace_requests;
  *replace_requests.Add() = make_ungrid_item(kVirtualTypeId, 8);
  auto replace_checked = grid.check_replace(config, std::move(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.replace(replace_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 0);
  CASE_EXPECT_EQ(grid.get_item_count(kVirtualTypeId), 8);
  verify_item_count_consistency(grid, kVirtualTypeId);

  CASE_EXPECT_TRUE(grid.load(config, make_ungrid_item(kVirtualTypeId, 3)));
  CASE_EXPECT_EQ(grid.get_item_count(kVirtualTypeId), 11);
  verify_item_count_consistency(grid, kVirtualTypeId);

  const auto* virtual_group = grid.get_group(kVirtualTypeId);
  CASE_EXPECT_TRUE(virtual_group != nullptr && !virtual_group->empty());
  if (!virtual_group || virtual_group->empty()) {
    return;
  }
  uint64_t virtual_entry_id = (*virtual_group->begin())->entry_id();

  // 客户端增量同步也必须在无位置模式中维护普通道具的 entry_id 与数量。
  call_apply_entries(grid, config, {}, {{virtual_entry_id, make_ungrid_item(kVirtualTypeId, 12)}});
  CASE_EXPECT_EQ(grid.get_item_count(kVirtualTypeId), 12);
  verify_item_count_consistency(grid, kVirtualTypeId);
  verify_find_entry_by_id(grid, virtual_entry_id, kVirtualTypeId, 12, 0);

  call_apply_entries(grid, config, {virtual_entry_id}, {});
  CASE_EXPECT_EQ(grid.get_item_count(kVirtualTypeId), 0);
  verify_item_count_consistency(grid, kVirtualTypeId);
  verify_not_find_entry_by_id(grid, virtual_entry_id);

  std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_sub_basic(kCoinTypeId, 1),
                                                          make_sub_basic(kVirtualTypeId, 1)};
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
  for (const auto& b : basics) {
    *basics_field.Add() = b;
  }
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
  CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed));
  CASE_EXPECT_EQ(success.size(), static_cast<size_t>(2));
  CASE_EXPECT_TRUE(failed.empty());
  if (success.size() == 2) {
    CASE_EXPECT_FALSE(success[0].position().grid_position().has_virtual_inventory());
    CASE_EXPECT_FALSE(success[1].position().grid_position().has_virtual_inventory());
  }
}

CASE_TEST(ItemGridAlgorithm, no_position_rejects_positional_items) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  ItemGridAddRequest setup_requests;
  *setup_requests.Add() = make_ungrid_item(kCoinTypeId, 5);
  auto setup_checked = grid.check_add(config, std::move(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  ItemGridAddRequest grid_item_requests;
  *grid_item_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 3, 4);
  auto grid_item_checked = grid.check_add(config, std::move(grid_item_requests));
  CASE_EXPECT_EQ(grid_item_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.add(grid_item_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 5);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 0);

  ItemGridAddRequest equipment_requests;
  *equipment_requests.Add() = make_equip_item(10001, 1, 2);
  auto equipment_checked = grid.check_add(config, std::move(equipment_requests));
  CASE_EXPECT_EQ(equipment_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.add(equipment_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_TRUE(grid.get_by_guid(10001) == nullptr);

  // replace 经由空 clone 复用 check_add，不能在 clone 中重新允许占格物品。
  ItemGridReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto replace_checked = grid.check_replace(config, std::move(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.replace(replace_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 5);

  CASE_EXPECT_FALSE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 0, 0)));
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 5);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 0);
}

CASE_TEST(ItemGridAlgorithm, no_position_rejects_position_operations) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  ItemGridAddRequest setup_requests;
  *setup_requests.Add() = make_ungrid_item(kCoinTypeId, 5);
  auto setup_checked = grid.check_add(config, std::move(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  const auto* coin_group = grid.get_group(kCoinTypeId);
  CASE_EXPECT_TRUE(coin_group != nullptr && !coin_group->empty());
  if (!coin_group || coin_group->empty()) {
    return;
  }

  ItemGridMoveRequest move_requests;
  move_requests.move_sub_entrys.push_back({(*coin_group->begin()), 1});
  auto move_checked = grid.check_move(config, std::move(move_requests));
  CASE_EXPECT_EQ(move_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.move(move_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 5);
  verify_item_count_consistency(grid, kCoinTypeId);

  std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_sub_basic(kItemTypeId_1x1, 1)};
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
  for (const auto& b : basics) {
    *basics_field.Add() = b;
  }
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
  CASE_EXPECT_FALSE(grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed));
  CASE_EXPECT_TRUE(success.empty());
}

CASE_TEST(ItemGridAlgorithm, check_item_position_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  init_test_grid(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  // 准备一个合法条目，供 sub、move 和 replace 状态不变断言使用。
  ItemGridAddRequest setup_requests;
  *setup_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto setup_checked = grid.check_add(config, std::move(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  // check_add
  state.rejected_position_x = 1;
  state.reset_call_counts();
  ItemGridAddRequest add_requests;
  *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
  auto add_checked = grid.check_add(config, std::move(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // check_sub
  state.rejected_position_x = 0;
  state.reset_call_counts();
  ItemGridSubRequest sub_requests;
  *sub_requests.Add() = make_sub_basic(kItemTypeId_1x1, 1, 0, 0);
  auto sub_checked = grid.check_sub(config, std::move(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  PROJECT_NAMESPACE_ID::DItemGridPosition source_position;
  source_position.mutable_user_inventory()->set_x(0);
  source_position.mutable_user_inventory()->set_y(0);
  auto source_entry = grid.get(source_position);
  CASE_EXPECT_TRUE(source_entry != nullptr);
  if (!source_entry) {
    return;
  }

  // check_move 的源位置
  state.rejected_position_x = 0;
  state.reset_call_counts();
  ItemGridMoveRequest source_move;
  source_move.move_sub_entrys.push_back({source_entry, 1});
  source_move.move_add_entrys.push_back({source_entry, make_inventory_target(1, 0), 1});
  auto source_move_checked = grid.check_move(config, std::move(source_move));
  CASE_EXPECT_EQ(source_move_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // check_move 的目标位置
  state.rejected_position_x = 1;
  state.reset_call_counts();
  ItemGridMoveRequest target_move;
  target_move.move_sub_entrys.push_back({source_entry, 1});
  target_move.move_add_entrys.push_back({source_entry, make_inventory_target(1, 0), 1});
  auto target_move_checked = grid.check_move(config, std::move(target_move));
  CASE_EXPECT_EQ(target_move_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 2);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // check_replace 经由 clone 的 check_add 也必须保留位置钩子。
  state.rejected_position_x = 2;
  state.reset_call_counts();
  ItemGridReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  auto replace_checked = grid.check_replace(config, std::move(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // load
  state.rejected_position_x = 3;
  state.reset_call_counts();
  CASE_EXPECT_FALSE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 3, 0)));
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);
}

CASE_TEST(ItemGridAlgorithm, on_check_add_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  init_test_grid(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  // check_add
  state.reject_all_add = true;
  ItemGridAddRequest add_requests;
  *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto add_checked = grid.check_add(config, std::move(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_add_calls, 1);
  CASE_EXPECT_TRUE(grid.is_empty());

  // check_replace 经由 clone 调用 on_check_add。
  state.reset_call_counts();
  ItemGridReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto replace_checked = grid.check_replace(config, std::move(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_add_calls, 1);
  CASE_EXPECT_TRUE(grid.is_empty());

  // find_positions_for_basics 使用 on_check_add 过滤候选位置并继续扫描。
  state.reject_all_add = false;
  state.rejected_add_position_x = 0;
  state.reset_call_counts();
  std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_sub_basic(kItemTypeId_1x1, 1)};
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
  for (const auto& b : basics) {
    *basics_field.Add() = b;
  }
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed;
  CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics_field, ignore_field, success, failed));
  CASE_EXPECT_EQ(success.size(), static_cast<size_t>(1));
  CASE_EXPECT_EQ(success[0].position().grid_position().user_inventory().x(), 1);
  CASE_EXPECT_TRUE(state.on_check_add_calls >= 2);
}

CASE_TEST(ItemGridAlgorithm, on_check_sub_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  init_test_grid(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  ItemGridAddRequest setup_requests;
  *setup_requests.Add() = make_grid_item(kItemTypeId_1x1, 2, 0, 0);
  auto setup_checked = grid.check_add(config, std::move(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  state.reject_sub = true;
  state.reset_call_counts();
  ItemGridSubRequest sub_requests;
  *sub_requests.Add() = make_sub_basic(kItemTypeId_1x1, 1, 0, 0);
  auto sub_checked = grid.check_sub(config, std::move(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_sub_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 2);
}

CASE_TEST(ItemGridAlgorithm, on_check_item_count_limit_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemGridAlgorithm>();
  auto& grid = *grid_ptr;
  init_test_grid(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  ItemGridAddRequest setup_requests;
  *setup_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto setup_checked = grid.check_add(config, std::move(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  state.reject_count_limit = true;

  // check_add
  state.reset_call_counts();
  ItemGridAddRequest add_requests;
  *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
  auto add_checked = grid.check_add(config, std::move(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // check_replace 经由 clone 调用数量上限钩子。
  state.reset_call_counts();
  ItemGridReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  auto replace_checked = grid.check_replace(config, std::move(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // load
  state.reset_call_counts();
  CASE_EXPECT_FALSE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 3, 0)));
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);

  // check_move 的 add-only 路径代表外部 Grid 移入，必须执行数量上限钩子。
  auto source_grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
  auto& source_grid = *source_grid_ptr;
  init_test_grid(source_grid);
  ItemGridAddRequest source_requests;
  *source_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto source_checked = source_grid.check_add(config, std::move(source_requests));
  CASE_EXPECT_EQ(source_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(source_grid.add(source_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  PROJECT_NAMESPACE_ID::DItemGridPosition source_position;
  source_position.mutable_user_inventory()->set_x(0);
  source_position.mutable_user_inventory()->set_y(0);
  auto source_entry = source_grid.get(source_position);
  CASE_EXPECT_TRUE(source_entry != nullptr);
  if (!source_entry) {
    return;
  }

  state.reset_call_counts();
  ItemGridMoveRequest move_requests;
  move_requests.move_add_entrys.push_back({source_entry, make_inventory_target(1, 0), 1});
  auto move_checked = grid.check_move(config, std::move(move_requests));
  CASE_EXPECT_EQ(move_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 1);
  CASE_EXPECT_EQ(source_grid.get_item_count(kItemTypeId_1x1), 1);
}

CASE_TEST(ItemGridAlgorithm, check_has) {
  auto config = make_test_config_group();

  // 占格道具按位置查询, GUID 道具按 GUID 查询。
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    init_test_grid(grid);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    ItemGridAddRequest add_requests;
    *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    *add_requests.Add() = make_equip_item(9001, 1, 0);
    auto add_checked = grid.check_add(config, std::move(add_requests));
    CASE_EXPECT_EQ(add_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemGridHasRequest has_requests;
    *has_requests.Add() = make_sub_basic(kItemTypeId_1x1, 5, 0, 0);
    *has_requests.Add() = make_equip_sub_by_guid(9001);
    auto has_result = grid.check_has(config, has_requests);
    CASE_EXPECT_EQ(has_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 5);

    ItemGridHasRequest repeated_position_requests;
    *repeated_position_requests.Add() = make_sub_basic(kItemTypeId_1x1, 2, 0, 0);
    *repeated_position_requests.Add() = make_sub_basic(kItemTypeId_1x1, 3, 0, 0);
    CASE_EXPECT_EQ(grid.check_has(config, repeated_position_requests).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemGridHasRequest insufficient_requests;
    *insufficient_requests.Add() = make_sub_basic(kItemTypeId_1x1, 6, 0, 0);
    auto insufficient_result = grid.check_has(config, insufficient_requests);
    CASE_EXPECT_EQ(insufficient_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(insufficient_result.failed_index, 0);

    ItemGridHasRequest missing_position_requests;
    *missing_position_requests.Add() = make_sub_basic(kItemTypeId_1x1, 1, 2, 0);
    auto missing_position_result = grid.check_has(config, missing_position_requests);
    CASE_EXPECT_EQ(missing_position_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND);
    CASE_EXPECT_EQ(missing_position_result.failed_index, 0);

    ItemGridHasRequest missing_guid_requests;
    *missing_guid_requests.Add() = make_equip_sub_by_guid(9002);
    auto missing_guid_result = grid.check_has(config, missing_guid_requests);
    CASE_EXPECT_EQ(missing_guid_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND);
    CASE_EXPECT_EQ(missing_guid_result.failed_index, 0);
  }

  // 无位置道具按类型总数查询, 同一批次的请求需要累计数量。
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemGridAlgorithm>();
    auto& grid = *grid_ptr;
    grid.init(ItemGridAlgorithmMode::kNoPosition, 0, 0, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    register_test_log_handler(grid);

    ItemGridAddRequest add_requests;
    *add_requests.Add() = make_ungrid_item(kCoinTypeId, 10);
    *add_requests.Add() = make_ungrid_item(kVirtualTypeId, 4);
    auto add_checked = grid.check_add(config, std::move(add_requests));
    CASE_EXPECT_EQ(add_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemGridHasRequest has_requests;
    *has_requests.Add() = make_sub_basic(kCoinTypeId, 7);
    *has_requests.Add() = make_sub_basic(kVirtualTypeId, 4);
    CASE_EXPECT_EQ(grid.check_has(config, has_requests).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemGridHasRequest repeated_type_requests;
    *repeated_type_requests.Add() = make_sub_basic(kCoinTypeId, 6);
    *repeated_type_requests.Add() = make_sub_basic(kCoinTypeId, 5);
    auto repeated_type_result = grid.check_has(config, repeated_type_requests);
    CASE_EXPECT_EQ(repeated_type_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(repeated_type_result.failed_index, 1);

    ItemGridHasRequest missing_type_requests;
    *missing_type_requests.Add() = make_sub_basic(kVirtualTypeId, 5);
    auto missing_type_result = grid.check_has(config, missing_type_requests);
    CASE_EXPECT_EQ(missing_type_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(missing_type_result.failed_index, 0);
  }
}

CASE_TEST(ItemGridContainer, check_has) {
  auto config = make_test_config_group();
  auto container_ptr = atfw::util::memory::make_strong_rc<DualGridContainer>();
  auto& container = *container_ptr;

  auto inventory_item = make_grid_item(kItemTypeId_1x1, 5, 1, 1);
  auto backpack_item = make_grid_item(kItemTypeId_1x1, 7, 2, 2);
  backpack_item.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_x(
      2);
  backpack_item.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_y(
      2);

  ItemGridAddRequest add_requests;
  *add_requests.Add() = inventory_item;
  *add_requests.Add() = backpack_item;
  auto add_checked = container.check_add(config, std::move(add_requests));
  CASE_EXPECT_EQ(add_checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(container.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto inventory_basic = make_sub_basic(kItemTypeId_1x1, 3, 1, 1);
  auto backpack_basic = make_sub_basic(kItemTypeId_1x1, 7, 2, 2);
  backpack_basic.mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_x(2);
  backpack_basic.mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_y(2);

  ItemGridHasRequest has_requests;
  *has_requests.Add() = inventory_basic;
  *has_requests.Add() = backpack_basic;
  CASE_EXPECT_EQ(container.check_has(config, has_requests).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  ItemGridHasRequest failed_requests;
  *failed_requests.Add() = inventory_basic;
  auto missing_backpack = backpack_basic;
  missing_backpack.mutable_position()->mutable_grid_position()->mutable_character_inventory()->set_x(3);
  *failed_requests.Add() = missing_backpack;
  auto failed_result = container.check_has(config, failed_requests);
  CASE_EXPECT_EQ(failed_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND);
  CASE_EXPECT_EQ(failed_result.failed_index, 1);
}
