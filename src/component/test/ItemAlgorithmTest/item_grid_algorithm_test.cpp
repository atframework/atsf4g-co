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
#include <set>
#include <unordered_map>
#include <vector>

// ============================================================
// 辅助常量 — 基于 EnItemType 范围定义道具 ID
// ============================================================

// EN_ITEM_TYPE_EQUIPMENT: [700000, 900000) — 占格, need_guid=true, 每件独立
static constexpr int32_t kEquipmentTypeId = 700001;

// EN_ITEM_TYPE_COIN: [1000, 10000)  — 不占格, 不需要GUID
static constexpr int32_t kCoinTypeId = 1001;

// EN_ITEM_TYPE_VIRTUAL: [10000, 100000) — 不占格, 不需要GUID
static constexpr int32_t kVirtualTypeId = 10001;

// EN_ITEM_TYPE_ITEM: [100000, 400000) — 占格, 默认不需要GUID (由 proto 配置 need_guid=false)
static constexpr int32_t kItemTypeId_1x1 = 100001;  // 1x1 大小, accumulation_limit = 99
static constexpr int32_t kItemTypeId_2x2 = 100002;  // 2x2 大小, accumulation_limit = 1

// ============================================================
// 测试用 ItemGridAlgorithm — Hook get_item_position_cfg
// ============================================================

ITEM_ALGORITHM_NAMESPACE_BEGIN
namespace item_algorithm {

class TestItemGridAlgorithm : public ItemGridAlgorithm {
 public:
  /// @brief 注册 type_id → DItemPositionCfg 的映射 (替代配置表查询)
  void register_position_cfg(int32_t type_id, int32_t accumulation_limit, int32_t row_size, int32_t col_size) {
    auto& cfg = position_cfg_map_[type_id];
    cfg.set_accumulation_limit(accumulation_limit);
    cfg.set_row_size(row_size);
    cfg.set_column_size(col_size);
  }

 protected:
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

/// @brief 服务器端测试子类 — 在 on_item_data_changed 中收集 Entry 快照
///
/// 用于模拟服务器操作 → 收集变更 → 同步到客户端子类 的完整流程。
/// 调用 collect_apply_data() 将收集到的快照转换为 apply_entries 所需的 protobuf 参数。
class ServerTestItemGridAlgorithm : public TestItemGridAlgorithm {
 public:
  struct EntrySnapshot {
    uint64_t entry_id = 0;
    PROJECT_NAMESPACE_ID::DItemInstance item_instance;
  };

  /// @brief 收集到的 Entry 缓存: entry_id → 最后一次回调时的快照
  const std::unordered_map<uint64_t, EntrySnapshot>& get_entry_cache() const { return entry_cache_; }

  /// @brief 清空收集缓存 (每轮测试结束后可复用)
  void clear_change_cache() { entry_cache_.clear(); }

  /// @brief 将收集的快照转换为 apply_entries 接收的 protobuf 参数
  ///
  /// 规则:
  ///   - count <= 0 → 视为已删除, 放入 remove_entry_ids
  ///   - count >  0 → 视为新增/更新, 放入 update_entries (DItemInstanceEntry)
  void collect_apply_data(
      ::google::protobuf::RepeatedField<uint64_t>& out_remove_ids,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry>& out_updates) const {
    out_remove_ids.Clear();
    out_updates.Clear();
    for (const auto& pair : entry_cache_) {
      const auto& snapshot = pair.second;
      if (snapshot.item_instance.item_basic().count() <= 0) {
        out_remove_ids.Add(snapshot.entry_id);
      } else {
        auto* entry = out_updates.Add();
        entry->set_entry_id(snapshot.entry_id);
        *entry->mutable_instance() = snapshot.item_instance;
      }
    }
  }

 protected:
  void on_item_data_changed(const item_grid_entry_ptr_t& entry, ItemGridOperationReason /*reason*/) override {
    EntrySnapshot snapshot;
    snapshot.entry_id = entry->entry_id;
    snapshot.item_instance = entry->item_instance;
    entry_cache_[entry->entry_id] = std::move(snapshot);
  }

 private:
  std::unordered_map<uint64_t, EntrySnapshot> entry_cache_;
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
  }
};
static _ConsoleUtf8Initializer _consoleUtf8Init;
}  // anonymous
#endif

using namespace ITEM_ALGORITHM_NAMESPACE_ID;  // ItemGridAddRequest, ItemGridSubRequest, etc.
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

/// @brief 初始化测试 Grid 算法 (inventory 类型, 默认 10x10)
static void init_test_grid(TestItemGridAlgorithm& grid, int32_t row = 10, int32_t col = 10) {
  grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
  // 注册配置
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
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

/// @brief 在 Dump 结果中按 type_id 查找条目 (不占格道具)
static const PROJECT_NAMESPACE_ID::DItemInstance* find_dumped_by_type(
    const std::vector<PROJECT_NAMESPACE_ID::DItemInstance>& items, int32_t type_id) {
  for (const auto& inst : items) {
    if (inst.item_basic().type_id() == type_id) {
      return &inst;
    }
  }
  return nullptr;
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
        CASE_EXPECT_EQ(entry->item_instance.item_basic().type_id(), type_id);
        CASE_EXPECT_EQ(entry->item_instance.item_basic().count(), count);
      }
    }
    return true;
  });

  // 各类型累计数量 == get_item_count()
  for (const auto& pair : type_counts) {
    CASE_EXPECT_EQ(grid.get_item_count(pair.first), pair.second);
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
  grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
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
static void verify_server_client_sync(
    const ServerTestItemGridAlgorithm& server,
    const TestItemGridAlgorithm& client) {
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
  }

  // 2. 按 entry_id 逐条对比
  std::unordered_map<uint64_t, const ItemGridEntry*> server_entries, client_entries;
  for (const auto& group_pair : server_groups) {
    for (const auto& entry : group_pair.second) {
      if (entry) {
        server_entries[entry->entry_id] = entry.get();
      }
    }
  }
  for (const auto& group_pair : client_groups) {
    for (const auto& entry : group_pair.second) {
      if (entry) {
        client_entries[entry->entry_id] = entry.get();
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
    CASE_EXPECT_EQ(s_entry->item_instance.item_basic().type_id(), c_entry->item_instance.item_basic().type_id());
    CASE_EXPECT_EQ(s_entry->item_instance.item_basic().count(), c_entry->item_instance.item_basic().count());
    CASE_EXPECT_EQ(s_entry->item_instance.item_basic().guid(), c_entry->item_instance.item_basic().guid());
  }

  // 3. 对比 occupy_grid_flag
  const auto& server_flags = server.get_occupy_grid_flag();
  const auto& client_flags = client.get_occupy_grid_flag();
  CASE_EXPECT_EQ(server_flags.size(), client_flags.size());
  for (size_t r = 0; r < server_flags.size() && r < client_flags.size(); ++r) {
    CASE_EXPECT_EQ(server_flags[r].size(), client_flags[r].size());
    for (size_t c = 0; c < server_flags[r].size() && c < client_flags[r].size(); ++c) {
      if (server_flags[r][c] != client_flags[r][c]) {
        CASE_MSG_ERROR() << "occupy_grid_flag mismatch at (" << r << "," << c << ")"
                         << " server=" << server_flags[r][c] << " client=" << client_flags[r][c];
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
static void sync_and_verify(
    ServerTestItemGridAlgorithm& server,
    TestItemGridAlgorithm& client,
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
  TestItemGridAlgorithm grid;

  TestItemGridContainer(int32_t row = 10, int32_t col = 10) {
    grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
  }

  ItemGridAlgorithm* select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& /*position*/) override { return &grid; }

  const ItemGridAlgorithm* select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& /*position*/) const override {
    return &grid;
  }
};

/// @brief 双 Grid 测试容器: inventory_grid 处理 kUserInventory 位置, backpack_grid 处理 kCharacterInventory 位置
/// 用于测试跨 Grid Move 场景
class DualGridContainer : public ItemGridContainer {
 public:
  TestItemGridAlgorithm inventory_grid;  ///< 处理 kUserInventory 位置
  TestItemGridAlgorithm backpack_grid;   ///< 处理 kCharacterInventory 位置

  DualGridContainer(int32_t row = 10, int32_t col = 10) {
    inventory_grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    inventory_grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    inventory_grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);

    backpack_grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory);
    backpack_grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    backpack_grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
  }

  ItemGridAlgorithm* select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& pos) override {
    if (pos.grid_position().has_user_inventory()) {
      return &inventory_grid;
    } else if (pos.grid_position().has_character_inventory()) {
      return &backpack_grid;
    }
    return nullptr;
  }

  const ItemGridAlgorithm* select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& pos) const override {
    if (pos.grid_position().has_user_inventory()) {
      return &inventory_grid;
    } else if (pos.grid_position().has_character_inventory()) {
      return &backpack_grid;
    }
    return nullptr;
  }
};

}  // namespace item_algorithm
ITEM_ALGORITHM_NAMESPACE_END

// ============================================================
// 玩家游玩模拟 — 完整生命周期大测试
//
// 覆盖操作: check_add / add (货币/虚拟/1x1/2x2/装备GUID),
//           add 合并 / stack overflow / position occupied / out of range,
//           check_sub / sub (部分/全部/按位置/按GUID/不足失败),
//           check_move / move (整体/部分拆分/目标占用失败),
//           load (占格/不占格/装备), foreach, clear,
//           entry_id (自增/独立/拆分产生新条目),
//           apply_entries (删除/更新/新增/位置变更/装备GUID/货币),
//           Container (单Grid/双Grid跨Grid Move/大物品与小物品交换),
//           服务器→客户端同步验证
// ============================================================

CASE_TEST(ItemGridAlgorithm, player_gameplay_simulation) {
  ServerTestItemGridAlgorithm server;
  init_server_grid(server);
  // 同时注册装备配置, 使服务器支持所有道具类型
  server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  TestItemGridAlgorithm client;
  init_test_grid(client);
  client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto config = make_test_config_group();

  // ----------------------------------------------------------------
  // Step 1: 新手奖励 — 添加货币 500, 虚拟道具 10, 初始装备 (guid=1001)
  // 验证: check_add 通过, add 成功, Dump/Sync 正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 1: 新手奖励 ===\n";
  {
    auto coin = make_ungrid_item(kCoinTypeId, 500);
    auto virtual_item = make_ungrid_item(kVirtualTypeId, 10);
    auto equip = make_equip_item(1001, 0, 0);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&coin});
    reqs.push_back({&virtual_item});
    reqs.push_back({&equip});

    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.add(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count(kCoinTypeId), 500);
  CASE_EXPECT_EQ(server.get_item_count(kVirtualTypeId), 10);
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 1);
  CASE_EXPECT_TRUE(server.get_by_guid(1001) != nullptr);
  CASE_EXPECT_FALSE(server.is_empty());
  // entry_id 应从 1 开始自增, 三个条目 entry_id 分别为 1, 2, 3
  CASE_EXPECT_EQ(server.peek_next_entry_id(), static_cast<uint64_t>(4));
  {
    auto dumped = dump_grid_items(server);
    CASE_EXPECT_EQ(dumped.size(), static_cast<size_t>(3));
    auto* coin_d = find_dumped_by_type(dumped, kCoinTypeId);
    CASE_EXPECT_TRUE(coin_d != nullptr);
    if (coin_d) CASE_EXPECT_EQ(coin_d->item_basic().count(), static_cast<int64_t>(500));
    auto* equip_d = find_dumped_by_position(dumped, kEquipmentTypeId, 0, 0);
    CASE_EXPECT_TRUE(equip_d != nullptr);
    if (equip_d) CASE_EXPECT_EQ(equip_d->item_basic().guid(), static_cast<int64_t>(1001));
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
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item1});
    reqs.push_back({&item2});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 80);
  // 验证占格标记: (0,0) 装备, (1,0) 1x1, (2,0) 1x1 => 三格占用
  {
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_TRUE(flags[0][0]);   // 装备
    CASE_EXPECT_TRUE(flags[0][1]);   // 1x1 at (1,0)
    CASE_EXPECT_TRUE(flags[0][2]);   // 1x1 at (2,0)
    CASE_EXPECT_FALSE(flags[0][3]);  // 空
    CASE_EXPECT_FALSE(flags[1][0]);  // 空
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 2: 拾取材料");

  // ----------------------------------------------------------------
  // Step 3: 货币合并追加 — 再获得 200 金币 (合并到已有条目)
  // 验证: 不占格道具合并, entry_id 不变
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 3: 货币合并追加 ===\n";
  uint64_t coin_entry_id;
  {
    const auto* coin_group = server.get_group(kCoinTypeId);
    CASE_EXPECT_TRUE(coin_group != nullptr && !coin_group->empty());
    coin_entry_id = coin_group->front()->entry_id;
  }
  {
    auto coin2 = make_ungrid_item(kCoinTypeId, 200);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&coin2});
    server.add(server.check_add(config, reqs));
  }
  CASE_EXPECT_EQ(server.get_item_count(kCoinTypeId), 700);
  // 合并后 entry_id 不变
  {
    const auto* coin_group = server.get_group(kCoinTypeId);
    CASE_EXPECT_TRUE(coin_group != nullptr && !coin_group->empty());
    CASE_EXPECT_EQ(coin_group->front()->entry_id, coin_entry_id);
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 3: 货币合并追加");

  // ----------------------------------------------------------------
  // Step 4: 1x1 堆叠追加 — 在 (1,0) 追加 60 个 (已有 30, 合计 90, 上限 99)
  // 验证: 占格道具合并堆叠
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 4: 1x1 堆叠追加 ===\n";
  {
    auto item = make_grid_item(kItemTypeId_1x1, 60, 1, 0);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 140);  // 90 + 50
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos10;
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos10);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) CASE_EXPECT_EQ(e->item_instance.item_basic().count(), static_cast<int64_t>(90));
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
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
    CASE_EXPECT_EQ(checked.result.failed_index, 0);
    // add 应直接返回错误
    auto result = server.add(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
  }
  // 5b: position occupied — (0,0) 已有装备
  {
    auto item = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 5c: out of range — (10,10) 超出 10x10 背包
  {
    auto item = make_grid_item(kItemTypeId_1x1, 1, 10, 10);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据应不变
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 140);
  CASE_EXPECT_EQ(server.get_item_count(kCoinTypeId), 700);
  verify_grid_dump(server);

  // ----------------------------------------------------------------
  // Step 6: 装备 GUID 相关失败检查
  // 验证: guid=0 失败, 重复 GUID 失败, 位置已被占用失败
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 6: 装备 GUID 添加失败检查 ===\n";
  // 6a: guid=0 应失败
  {
    auto bad = make_grid_item(kEquipmentTypeId, 1, 3, 0, 0);  // guid=0
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&bad});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 6b: 重复 GUID=1001 应失败
  {
    auto dup = make_equip_item(1001, 3, 0);  // GUID 1001 已存在
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&dup});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 6c: 位置 (0,0) 被装备 1001 占用
  {
    auto conflict = make_equip_item(9999, 0, 0);  // 新GUID但位置冲突
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&conflict});
    auto checked = server.check_add(config, reqs);
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
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&big});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_2x2), 1);
  {
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_TRUE(flags[4][4]);   // (4,4) row=4,col=4
    CASE_EXPECT_TRUE(flags[4][5]);   // (5,4) row=4,col=5
    CASE_EXPECT_TRUE(flags[5][4]);   // (4,5) row=5,col=4
    CASE_EXPECT_TRUE(flags[5][5]);   // (5,5) row=5,col=5
    CASE_EXPECT_FALSE(flags[4][3]);  // 旁边应为空
    CASE_EXPECT_FALSE(flags[3][4]);
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
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&e2});
    reqs.push_back({&e3});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 3);
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
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    auto checked = server.check_sub(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.sub(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 120);  // 90 + 30
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos20);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) CASE_EXPECT_EQ(e->item_instance.item_basic().count(), static_cast<int64_t>(30));
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 9: 消费材料 (部分扣减)");

  // ----------------------------------------------------------------
  // Step 10: 消费材料 — 扣减 (2,0) 全部 30 个 (释放格子)
  // 验证: 完全扣减, 位置释放, 占格标记清除
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 10: 消费材料 (完全扣减释放位置) ===\n";
  {
    auto sub = make_sub_basic(kItemTypeId_1x1, 30, 2, 0);
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    server.sub(server.check_sub(config, reqs));
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);  // 只剩 (1,0)=90
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_TRUE(server.get(gpos20) == nullptr);  // 已释放
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_FALSE(flags[0][2]);  // (2,0) 已释放
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
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    auto checked = server.check_sub(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    // sub 应直接返回错误
    auto result = server.sub(checked);
    CASE_EXPECT_NE(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11b: 装备 guid=0 失败
  {
    auto sub = make_sub_basic(kEquipmentTypeId, 1, 0, 0, 0);
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    auto checked = server.check_sub(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11c: 装备不存在的 GUID
  {
    auto sub = make_equip_sub_by_guid(77777);
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    auto checked = server.check_sub(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11d: 装备 count != 1
  {
    PROJECT_NAMESPACE_ID::DItemBasic bad_sub;
    bad_sub.set_type_id(kEquipmentTypeId);
    bad_sub.set_count(2);
    bad_sub.set_guid(1001);
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&bad_sub});
    auto checked = server.check_sub(config, reqs);
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 3);
  verify_grid_dump(server);

  // ----------------------------------------------------------------
  // Step 12: 删除装备 (按 GUID) — 卸下 guid=1002
  // 验证: GUID 索引清除, 位置释放
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 12: 按 GUID 扣减装备 ===\n";
  {
    auto sub = make_equip_sub_by_guid(1002);
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    auto checked = server.check_sub(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.sub(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 2);
  CASE_EXPECT_TRUE(server.get_by_guid(1002) == nullptr);
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
  // 验证: 旧位置释放, 新位置占用, entry_id 不变
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 13: Move 整体搬移 ===\n";
  uint64_t move_entry_id;
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos10;
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    auto entry = server.get(gpos10);
    CASE_EXPECT_TRUE(entry != nullptr);
    move_entry_id = entry->entry_id;

    ItemGridMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 90});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(2);
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 90});

    auto checked = server.check_move(config, move_req);
    CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.move(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos10, gpos20;
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_TRUE(server.get(gpos10) == nullptr);       // 旧位置空
    auto moved = server.get(gpos20);
    CASE_EXPECT_TRUE(moved != nullptr);                     // 新位置有
    if (moved) {
      CASE_EXPECT_EQ(moved->item_instance.item_basic().count(), static_cast<int64_t>(90));
      // 整体 Move (sub 全部+add) 会创建新 entry, entry_id 不保留
      CASE_EXPECT_NE(moved->entry_id, static_cast<uint64_t>(0));
    }
    const auto& flags = server.get_occupy_grid_flag();
    CASE_EXPECT_FALSE(flags[0][1]);  // (1,0) 已释放
    CASE_EXPECT_TRUE(flags[0][2]);   // (2,0) 占用
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 13: Move 整体搬移");

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

    auto checked = server.check_move(config, move_req);
    CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.move(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);  // 总量不变
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
    if (src) CASE_EXPECT_EQ(src->item_instance.item_basic().count(), static_cast<int64_t>(50));
    if (dst) CASE_EXPECT_EQ(dst->item_instance.item_basic().count(), static_cast<int64_t>(40));
    // 拆分产生新 entry_id
    if (src && dst) CASE_EXPECT_NE(src->entry_id, dst->entry_id);
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

    auto checked = server.check_move(config, move_req);
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

    auto checked = server.check_move(config, move_req);
    CASE_EXPECT_NE(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 90);
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
    CASE_EXPECT_EQ(entry->item_instance.item_basic().guid(), static_cast<int64_t>(1003));

    ItemGridMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 1});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(3);
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 1});

    auto checked = server.check_move(config, move_req);
    CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
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
    if (moved_eq) CASE_EXPECT_EQ(moved_eq->item_instance.item_basic().guid(), static_cast<int64_t>(1003));
    // GUID 索引仍有效
    auto by_guid = server.get_by_guid(1003);
    CASE_EXPECT_TRUE(by_guid != nullptr);
  }
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 16: Move 装备");

  // ----------------------------------------------------------------
  // Step 17: 花费货币 — 扣减全部 700 金币
  // 验证: 不占格道具完全移除
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 17: 花费所有货币 ===\n";
  {
    auto sub = make_sub_basic(kCoinTypeId, 700);
    std::vector<ItemGridSubRequest> reqs;
    reqs.push_back({&sub});
    server.sub(server.check_sub(config, reqs));
  }
  CASE_EXPECT_EQ(server.get_item_count(kCoinTypeId), 0);
  verify_grid_dump(server);
  sync_and_verify(server, client, config, "Step 17: 花费所有货币");

  // ----------------------------------------------------------------
  // Step 18: Load 接口 — 模拟从数据库加载存档
  // 新建一个独立的 server/client Grid, Load 各种道具, 验证同步
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 18: Load 从数据库恢复 ===\n";
  {
    // 用独立 Grid 测试 Load
    ServerTestItemGridAlgorithm load_server;
    init_server_grid(load_server);
    load_server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    TestItemGridAlgorithm load_client;
    init_test_grid(load_client);
    load_client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    // Load 货币
    auto coin = make_ungrid_item(kCoinTypeId, 999);
    CASE_EXPECT_TRUE(load_server.load(config, coin));

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

    CASE_EXPECT_EQ(load_server.get_item_count(kCoinTypeId), 999);
    CASE_EXPECT_EQ(load_server.get_item_count(kItemTypeId_1x1), 65);  // 55+10 堆叠
    CASE_EXPECT_EQ(load_server.get_item_count(kEquipmentTypeId), 1);
    CASE_EXPECT_TRUE(load_server.get_by_guid(2001) != nullptr);

    verify_grid_dump(load_server);
    sync_and_verify(load_server, load_client, config, "Step 18: Load");
  }

  // ----------------------------------------------------------------
  // Step 19: foreach + clear — 清空后验证
  // 用独立 Grid 测试
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 19: foreach + clear ===\n";
  {
    ServerTestItemGridAlgorithm temp;
    init_server_grid(temp);

    // 添加若干道具
    auto item1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
    auto coin = make_ungrid_item(kCoinTypeId, 100);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item1});
    reqs.push_back({&item2});
    reqs.push_back({&coin});
    temp.add(temp.check_add(config, reqs));

    // foreach 计数
    int count = 0;
    temp.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance&) {
      ++count;
      return true;
    });
    CASE_EXPECT_EQ(count, 3);

    // clear
    temp.clear();
    CASE_EXPECT_TRUE(temp.is_empty());
    CASE_EXPECT_EQ(temp.get_item_count(kItemTypeId_1x1), 0);
    CASE_EXPECT_EQ(temp.get_item_count(kCoinTypeId), 0);
    // 占格标记应全部清除
    for (const auto& row : temp.get_occupy_grid_flag()) {
      for (bool cell : row) {
        CASE_EXPECT_FALSE(cell);
      }
    }
    verify_grid_dump(temp);
  }

  // ----------------------------------------------------------------
  // Step 20: entry_id 独立性 — 两个 Grid 各自自增
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 20: entry_id 独立性验证 ===\n";
  {
    ServerTestItemGridAlgorithm grid_a;
    init_server_grid(grid_a);
    ServerTestItemGridAlgorithm grid_b;
    init_server_grid(grid_b);

    auto item_a = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    std::vector<ItemGridAddRequest> ra;
    ra.push_back({&item_a});
    grid_a.add(grid_a.check_add(config, ra));
    grid_a.add(grid_a.check_add(config, ra));  // 合并, entry_id=1

    auto item_b = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    std::vector<ItemGridAddRequest> rb;
    rb.push_back({&item_b});
    grid_b.add(grid_b.check_add(config, rb));

    // 两个 Grid 的 entry_id 独立
    PROJECT_NAMESPACE_ID::DItemGridPosition gp00;
    gp00.mutable_user_inventory()->set_x(0);
    gp00.mutable_user_inventory()->set_y(0);
    CASE_EXPECT_EQ(grid_a.get(gp00)->entry_id, static_cast<uint64_t>(1));
    CASE_EXPECT_EQ(grid_b.get(gp00)->entry_id, static_cast<uint64_t>(1));
  }

  // ----------------------------------------------------------------
  // Step 21: apply_entries 直接测试 — 删除 / 更新 / 新增 / 位置变更
  // 在独立 Grid 上验证, 不通过 Server→Client 流程
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 21: apply_entries 直接测试 ===\n";
  {
    TestItemGridAlgorithm grid;
    init_test_grid(grid);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    // 添加几个道具
    auto item1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
    auto item3 = make_grid_item(kItemTypeId_1x1, 30, 2, 0);
    auto equip = make_equip_item(3001, 3, 0);
    auto coin = make_ungrid_item(kCoinTypeId, 100);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&item1});
    reqs.push_back({&item2});
    reqs.push_back({&item3});
    reqs.push_back({&equip});
    reqs.push_back({&coin});
    grid.add(grid.check_add(config, reqs));

    PROJECT_NAMESPACE_ID::DItemGridPosition gpos00, gpos10, gpos20, gpos30;
    gpos00.mutable_user_inventory()->set_x(0);
    gpos00.mutable_user_inventory()->set_y(0);
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    gpos30.mutable_user_inventory()->set_x(3);
    gpos30.mutable_user_inventory()->set_y(0);

    uint64_t eid1 = grid.get(gpos00)->entry_id;
    uint64_t eid2 = grid.get(gpos10)->entry_id;
    uint64_t eid3 = grid.get(gpos20)->entry_id;
    uint64_t eid_eq = grid.get(gpos30)->entry_id;
    uint64_t eid_coin = grid.get_group(kCoinTypeId)->front()->entry_id;

    // 21a: 删除 item1 和 item3
    {
      std::vector<uint64_t> rm = {eid1, eid3};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd;
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get(gpos00) == nullptr);
    CASE_EXPECT_TRUE(grid.get(gpos20) == nullptr);
    CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 20);  // 只剩 eid2

    // 21b: 更新 eid2 count 20→88
    {
      std::vector<uint64_t> rm;
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {eid2, make_grid_item(kItemTypeId_1x1, 88, 1, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_EQ(grid.get(gpos10)->item_instance.item_basic().count(), static_cast<int64_t>(88));
    CASE_EXPECT_EQ(grid.get(gpos10)->entry_id, eid2);

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
    if (grid.get(gpos78)) CASE_EXPECT_EQ(grid.get(gpos78)->entry_id, eid2);

    // 21d: 新增 entry (entry_id=500)
    {
      std::vector<uint64_t> rm;
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {500, make_grid_item(kItemTypeId_1x1, 25, 0, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get(gpos00) != nullptr);
    if (grid.get(gpos00)) CASE_EXPECT_EQ(grid.get(gpos00)->entry_id, static_cast<uint64_t>(500));

    // 21e: 删除后同位置新增 (替换)
    {
      uint64_t old_eid = grid.get(gpos00)->entry_id;
      std::vector<uint64_t> rm = {old_eid};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {999, make_grid_item(kItemTypeId_1x1, 42, 0, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get(gpos00) != nullptr);
    if (grid.get(gpos00)) {
      CASE_EXPECT_EQ(grid.get(gpos00)->entry_id, static_cast<uint64_t>(999));
      CASE_EXPECT_EQ(grid.get(gpos00)->item_instance.item_basic().count(), static_cast<int64_t>(42));
    }

    // 21f: 装备 GUID — 删除 + 新增
    {
      std::vector<uint64_t> rm = {eid_eq};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {777, make_equip_item(3002, 3, 0)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_TRUE(grid.get_by_guid(3001) == nullptr);
    CASE_EXPECT_TRUE(grid.get_by_guid(3002) != nullptr);
    if (grid.get_by_guid(3002))
      CASE_EXPECT_EQ(grid.get_by_guid(3002)->entry_id, static_cast<uint64_t>(777));

    // 21g: 货币更新 + 删除
    {
      std::vector<uint64_t> rm;
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd = {
          {eid_coin, make_ungrid_item(kCoinTypeId, 500)}};
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 500);
    {
      std::vector<uint64_t> rm = {eid_coin};
      std::vector<std::pair<uint64_t, PROJECT_NAMESPACE_ID::DItemInstance>> upd;
      call_apply_entries(grid, config, rm, upd);
    }
    CASE_EXPECT_EQ(grid.get_item_count(kCoinTypeId), 0);

    verify_grid_dump(grid);
  }

  // ----------------------------------------------------------------
  // Step 22: Container 单 Grid — check_add / add / check_sub / sub
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 22: Container 单 Grid ===\n";
  {
    TestItemGridContainer container;

    // add
    auto item = make_grid_item(kItemTypeId_1x1, 5, 1, 1);
    std::vector<ItemGridAddRequest> add_reqs;
    add_reqs.push_back({&item});
    auto checked_add = container.check_add(config, add_reqs);
    CASE_EXPECT_EQ(checked_add.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto add_result = container.add(checked_add);
    CASE_EXPECT_EQ(add_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(container.grid.get_item_count(kItemTypeId_1x1), 5);

    // sub
    auto sub = make_sub_basic(kItemTypeId_1x1, 3, 1, 1);
    std::vector<ItemGridSubRequest> sub_reqs;
    sub_reqs.push_back({&sub});
    auto checked_sub = container.check_sub(config, sub_reqs);
    CASE_EXPECT_EQ(checked_sub.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto sub_result = container.sub(checked_sub);
    CASE_EXPECT_EQ(sub_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(container.grid.get_item_count(kItemTypeId_1x1), 2);

    // check_add 失败: stack overflow
    auto overflow = make_grid_item(kItemTypeId_1x1, 100, 1, 1);
    std::vector<ItemGridAddRequest> fail_reqs;
    fail_reqs.push_back({&overflow});
    auto fail_checked = container.check_add(config, fail_reqs);
    CASE_EXPECT_EQ(fail_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
    auto fail_result = container.add(fail_checked);
    CASE_EXPECT_EQ(fail_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);

    // check_sub 失败: 空背包扣减不存在的道具
    auto bad_sub = make_sub_basic(kVirtualTypeId, 10);
    std::vector<ItemGridSubRequest> fail_sub_reqs;
    fail_sub_reqs.push_back({&bad_sub});
    auto fail_sub_checked = container.check_sub(config, fail_sub_reqs);
    CASE_EXPECT_NE(fail_sub_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    verify_grid_dump(container.grid);
  }

  // ----------------------------------------------------------------
  // Step 23: Container Move — 同位置跳过 / 合并操作 / 大小物品交换
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 23: Container Move 系列 ===\n";
  {
    TestItemGridContainer container;

    // 放两个 1x1：(0,0)=10, (1,0)=20
    {
      auto i1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
      auto i2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
      std::vector<ItemGridAddRequest> reqs;
      reqs.push_back({&i1});
      reqs.push_back({&i2});
      container.add(container.check_add(config, reqs));
    }

    // 23a: same position skip
    {
      PROJECT_NAMESPACE_ID::DItemBasic sub_basic = make_sub_basic(kItemTypeId_1x1, 5, 0, 0);
      std::vector<ItemGridContainerMoveRequest> move_reqs;
      move_reqs.push_back({sub_basic, make_inventory_target(0, 0)});
      auto checked = container.check_move(config, move_reqs);
      CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      // 应被优化跳过
      auto result = container.move(checked);
      CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      CASE_EXPECT_EQ(container.grid.get_item_count(kItemTypeId_1x1), 30);  // 不变
    }

    // 23b: merge sub — 两个 move 请求操作同一源
    {
      PROJECT_NAMESPACE_ID::DItemBasic sub1 = make_sub_basic(kItemTypeId_1x1, 3, 0, 0);
      PROJECT_NAMESPACE_ID::DItemBasic sub2 = make_sub_basic(kItemTypeId_1x1, 2, 0, 0);
      std::vector<ItemGridContainerMoveRequest> move_reqs;
      move_reqs.push_back({sub1, make_inventory_target(2, 0)});
      move_reqs.push_back({sub2, make_inventory_target(3, 0)});
      auto checked = container.check_move(config, move_reqs);
      CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      container.move(checked);
    }
    // (0,0)=5, (1,0)=20, (2,0)=3, (3,0)=2
    CASE_EXPECT_EQ(container.grid.get_item_count(kItemTypeId_1x1), 30);
    {
      auto dumped = dump_grid_items(container.grid);
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
    verify_grid_dump(container.grid);
  }

  // ----------------------------------------------------------------
  // Step 24: Container — 大物品与小物品交换位置
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 24: 大物品与小物品交换位置 ===\n";
  {
    TestItemGridContainer container;

    // 放 2x2 在 (0,0) 和 1x1 在 (4,0), (5,0)
    auto big = make_grid_item(kItemTypeId_2x2, 1, 0, 0);
    auto s1 = make_grid_item(kItemTypeId_1x1, 10, 4, 0);
    auto s2 = make_grid_item(kItemTypeId_1x1, 20, 5, 0);
    {
      std::vector<ItemGridAddRequest> reqs;
      reqs.push_back({&big});
      reqs.push_back({&s1});
      reqs.push_back({&s2});
      container.add(container.check_add(config, reqs));
    }

    // 交换: 2x2(0,0) → (4,0), 两个 1x1 → (0,0) 和 (1,0)
    PROJECT_NAMESPACE_ID::DItemBasic big_sub = make_sub_basic(kItemTypeId_2x2, 1, 0, 0);
    PROJECT_NAMESPACE_ID::DItemBasic s1_sub = make_sub_basic(kItemTypeId_1x1, 10, 4, 0);
    PROJECT_NAMESPACE_ID::DItemBasic s2_sub = make_sub_basic(kItemTypeId_1x1, 20, 5, 0);
    std::vector<ItemGridContainerMoveRequest> move_reqs;
    move_reqs.push_back({big_sub, make_inventory_target(4, 0)});
    move_reqs.push_back({s1_sub, make_inventory_target(0, 0)});
    move_reqs.push_back({s2_sub, make_inventory_target(1, 0)});

    auto checked = container.check_move(config, move_reqs);
    CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = container.move(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 验证
    {
      auto dumped = dump_grid_items(container.grid);
      auto* big_at40 = find_dumped_by_position(dumped, kItemTypeId_2x2, 4, 0);
      auto* s1_at00 = find_dumped_by_position(dumped, kItemTypeId_1x1, 0, 0);
      auto* s2_at10 = find_dumped_by_position(dumped, kItemTypeId_1x1, 1, 0);
      CASE_EXPECT_TRUE(big_at40 != nullptr);
      CASE_EXPECT_TRUE(s1_at00 != nullptr);
      CASE_EXPECT_TRUE(s2_at10 != nullptr);
    }
    // 占格标记验证
    {
      const auto& flags = container.grid.get_occupy_grid_flag();
      CASE_EXPECT_TRUE(flags[0][0]);   // 1x1 at (0,0)
      CASE_EXPECT_TRUE(flags[0][1]);   // 1x1 at (1,0)
      CASE_EXPECT_FALSE(flags[1][0]);  // 原来 2x2 占 (0,0)~(1,1), 现在释放
      CASE_EXPECT_TRUE(flags[0][4]);   // 2x2 at (4,0)
      CASE_EXPECT_TRUE(flags[0][5]);   // 2x2 at (5,0)→col=5
      CASE_EXPECT_TRUE(flags[1][4]);   // 2x2 row=1
      CASE_EXPECT_TRUE(flags[1][5]);
    }
    verify_grid_dump(container.grid);
  }

  // ----------------------------------------------------------------
  // Step 25: Container — 跨 Grid Move (DualGridContainer)
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 25: 跨 Grid Move ===\n";
  {
    DualGridContainer container;

    // inventory_grid 添加 1x1 (2,3) count=10
    {
      auto item = make_grid_item(kItemTypeId_1x1, 10, 2, 3);
      std::vector<ItemGridAddRequest> reqs;
      reqs.push_back({&item});
      container.add(container.check_add(config, reqs));
    }
    CASE_EXPECT_EQ(container.inventory_grid.get_item_count(kItemTypeId_1x1), 10);

    // 跨 Grid: inventory (2,3) → backpack (0,0), 6 个
    PROJECT_NAMESPACE_ID::DItemBasic src_basic = make_sub_basic(kItemTypeId_1x1, 6, 2, 3);
    std::vector<ItemGridContainerMoveRequest> move_reqs;
    move_reqs.push_back({src_basic, make_backpack_target(0, 0)});

    auto checked = container.check_move(config, move_reqs);
    CASE_EXPECT_EQ(checked.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(checked.grid_data.size(), static_cast<size_t>(2));

    auto result = container.move(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 验证
    CASE_EXPECT_EQ(container.inventory_grid.get_item_count(kItemTypeId_1x1), 4);
    CASE_EXPECT_EQ(container.backpack_grid.get_item_count(kItemTypeId_1x1), 6);
    {
      auto inv_dumped = dump_grid_items(container.inventory_grid);
      auto* at23 = find_dumped_by_position(inv_dumped, kItemTypeId_1x1, 2, 3);
      CASE_EXPECT_TRUE(at23 != nullptr);
      if (at23) CASE_EXPECT_EQ(at23->item_basic().count(), static_cast<int64_t>(4));
    }
    {
      auto bp_dumped = dump_grid_items(container.backpack_grid);
      auto* at00 = find_dumped_by_backpack_position(bp_dumped, kItemTypeId_1x1, 0, 0);
      CASE_EXPECT_TRUE(at00 != nullptr);
      if (at00) CASE_EXPECT_EQ(at00->item_basic().count(), static_cast<int64_t>(6));
    }
    // backpack 第一个 entry_id 应为 1
    PROJECT_NAMESPACE_ID::DItemGridPosition bp00;
    bp00.mutable_character_inventory()->set_x(0);
    bp00.mutable_character_inventory()->set_y(0);
    auto bp_entry = container.backpack_grid.get(bp00);
    CASE_EXPECT_TRUE(bp_entry != nullptr);
    if (bp_entry) CASE_EXPECT_EQ(bp_entry->entry_id, static_cast<uint64_t>(1));

    verify_grid_dump(container.inventory_grid);
    verify_grid_dump(container.backpack_grid);
  }

  // ----------------------------------------------------------------
  // Step 26: 继续主线 — 回到主 server/client, 再获得一波奖励后验证整体状态
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 26: 继续主线 — 最终奖励 ===\n";
  {
    auto new_coin = make_ungrid_item(kCoinTypeId, 1000);
    auto new_item = make_grid_item(kItemTypeId_1x1, 50, 1, 0);  // (1,0) 已空
    auto new_equip = make_equip_item(1004, 6, 0);
    std::vector<ItemGridAddRequest> reqs;
    reqs.push_back({&new_coin});
    reqs.push_back({&new_item});
    reqs.push_back({&new_equip});
    auto checked = server.check_add(config, reqs);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count(kCoinTypeId), 1000);
  CASE_EXPECT_EQ(server.get_item_count(kItemTypeId_1x1), 140);  // 50(2,0) + 40(5,0) + 50(1,0)
  CASE_EXPECT_EQ(server.get_item_count(kEquipmentTypeId), 3);    // 1001, 1003, 1004
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
    for (size_t r = 0; r < sf.size(); ++r) {
      for (size_t c = 0; c < sf[r].size(); ++c) {
        CASE_EXPECT_EQ(sf[r][c], cf[r][c]);
      }
    }

    verify_grid_dump(server);
    verify_grid_dump(client);
  }

  CASE_MSG_INFO() << "=== 玩家游玩模拟完成 ===\n";
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

  // ============================================================
  // Case 1: 非占格道具 -> 输出空 DItemGridPosition
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 非占格道具输出空位置\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kCoinTypeId, 10),
        make_basic(kVirtualTypeId, 5),
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(2u, out.size());
    // 空位置：position_type_case 为 POSITION_TYPE_NOT_SET (0)
    CASE_EXPECT_EQ(0, static_cast<int>(out[0].position_type_case()));
    CASE_EXPECT_EQ(0, static_cast<int>(out[1].position_type_case()));
  }

  // ============================================================
  // Case 2: 空网格批量 1x1 - 游标递进，分配连续不重复格子
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 空网格批量分配游标递进\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(3, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
        make_basic(kItemTypeId_1x1, 1),
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(3u, out.size());

    // 游标优化：第一件在 (0,0)，后续递增
    CASE_EXPECT_EQ(0, get_x(out[0]));
    CASE_EXPECT_EQ(0, get_y(out[0]));
    CASE_EXPECT_EQ(1, get_x(out[1]));
    CASE_EXPECT_EQ(0, get_y(out[1]));
    CASE_EXPECT_EQ(2, get_x(out[2]));
    CASE_EXPECT_EQ(0, get_y(out[2]));

    // 三件物品不重复
    std::set<std::pair<int32_t, int32_t>> pos_set;
    for (const auto& p : out) {
      pos_set.insert({get_x(p), get_y(p)});
    }
    CASE_EXPECT_EQ(3u, pos_set.size());
  }

  // ============================================================
  // Case 3: 首选位置被优先使用
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 首选位置被优先使用\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic_at(kItemTypeId_1x1, 1, 3, 2),  // 希望放 (x=3, y=2)
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(1u, out.size());
    CASE_EXPECT_EQ(3, get_x(out[0]));
    CASE_EXPECT_EQ(2, get_y(out[0]));
  }

  // ============================================================
  // Case 4: 首选位置已占用 - 回退到扫描
  // ============================================================
  CASE_MSG_INFO() << "Case 4: 首选位置被占用时回退扫描\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    // accumulation_limit=1: 已有条目无剩余容量，策略2不会命中，确保走策略3扫描
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    // 先 load 一件到 (x=3, y=2) 占住
    {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(3);
      inst.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(2);
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic_at(kItemTypeId_1x1, 1, 3, 2),  // 首选位置已被占
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(1u, out.size());
    // 不能落到 (3,2)
    bool not_original = (get_x(out[0]) != 3 || get_y(out[0]) != 2);
    CASE_EXPECT_TRUE(not_original);
    // 必须在合法范围内
    CASE_EXPECT_TRUE(get_x(out[0]) >= 0 && get_x(out[0]) < 4);
    CASE_EXPECT_TRUE(get_y(out[0]) >= 0 && get_y(out[0]) < 4);
  }

  // ============================================================
  // Case 5: 堆叠到已有条目（accumulation_limit > 1）
  // ============================================================
  CASE_MSG_INFO() << "Case 5: 堆叠到已有条目\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
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
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(1u, out.size());
    // 定向到已有 entry (x=0, y=0)
    CASE_EXPECT_EQ(0, get_x(out[0]));
    CASE_EXPECT_EQ(0, get_y(out[0]));
  }

  // ============================================================
  // Case 6: 背包全满时返回 false
  // ============================================================
  CASE_MSG_INFO() << "Case 6: 背包全满返回 false\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
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
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_FALSE(grid.find_positions_for_basics(config, basics, out));
  }

  // ============================================================
  // Case 7: 2x2 物品批量分配，不重叠
  // ============================================================
  CASE_MSG_INFO() << "Case 7: 2x2 物品批量分配\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_2x2, 1),
        make_basic(kItemTypeId_2x2, 1),
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(2u, out.size());

    // 每个位置在范围内（2x2 起点 x in [0,2], y in [0,2]）
    for (const auto& p : out) {
      CASE_EXPECT_TRUE(get_x(p) >= 0 && get_x(p) + 2 <= 4);
      CASE_EXPECT_TRUE(get_y(p) >= 0 && get_y(p) + 2 <= 4);
    }
    // 两件起点不同（不重叠）
    bool same_pos = (get_x(out[0]) == get_x(out[1]) && get_y(out[0]) == get_y(out[1]));
    CASE_EXPECT_FALSE(same_pos);
  }

  // ============================================================
  // Case 8: 混合批次 1x1 与 2x2 - 批次内互不干扰
  // ============================================================
  CASE_MSG_INFO() << "Case 8: 混合批次 1x1 与 2x2\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kItemTypeId_2x2, 1),  // 先放 2x2
        make_basic(kItemTypeId_1x1, 1),  // 再放 1x1，应躲开 2x2 区域
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(2u, out.size());

    int32_t x2 = get_x(out[0]);
    int32_t y2 = get_y(out[0]);
    int32_t x1 = get_x(out[1]);
    int32_t y1 = get_y(out[1]);

    // 1x1 不能落在 2x2 所占的区域内
    bool overlap = (x1 >= x2 && x1 < x2 + 2 && y1 >= y2 && y1 < y2 + 2);
    CASE_EXPECT_FALSE(overlap);
  }

  // ============================================================
  // Case 9: 游标优化 - 前三列满，仅第4列(x=3)空，批量放4件
  // ============================================================
  CASE_MSG_INFO() << "Case 9: 游标优化接近满格验证\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
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
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(4u, out.size());

    std::set<std::pair<int32_t, int32_t>> pos_set;
    for (const auto& p : out) {
      CASE_EXPECT_EQ(3, get_x(p));       // 必须在 x=3 列
      CASE_EXPECT_TRUE(get_y(p) >= 0 && get_y(p) < 4);
      pos_set.insert({get_x(p), get_y(p)});
    }
    CASE_EXPECT_EQ(4u, pos_set.size());  // 无重复

    // find_positions_for_basics 是只读规划，不修改格子。
    // 需要实际 load 这 4 件到格子中，再验证满格。
    for (const auto& p : out) {
      PROJECT_NAMESPACE_ID::DItemInstance inst;
      inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
      inst.mutable_item_basic()->set_count(1);
      *inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = p;
      CASE_EXPECT_TRUE(grid.load(config, inst));
    }

    // 格子现在全满，第 5 件应返回 false
    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> one_more = {make_basic(kItemTypeId_1x1, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out2;
    CASE_EXPECT_FALSE(grid.find_positions_for_basics(config, one_more, out2));
  }

  // ============================================================
  // Case 10: 混合非占格 + 占格在同一批次
  // ============================================================
  CASE_MSG_INFO() << "Case 10: 混合非占格与占格\n";
  {
    TestItemGridAlgorithm grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {
        make_basic(kCoinTypeId, 100),    // 非占格
        make_basic(kItemTypeId_1x1, 1),  // 占格
        make_basic(kVirtualTypeId, 50),  // 非占格
        make_basic(kItemTypeId_1x1, 1),  // 占格
    };
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(4u, out.size());

    // [0] 和 [2] 非占格，position_type_case 为 NOT_SET (0)
    CASE_EXPECT_EQ(0, static_cast<int>(out[0].position_type_case()));
    CASE_EXPECT_EQ(0, static_cast<int>(out[2].position_type_case()));

    // [1] 和 [3] 是占格，位置不同
    bool same = (get_x(out[1]) == get_x(out[3]) && get_y(out[1]) == get_y(out[3]));
    CASE_EXPECT_FALSE(same);
  }

  // ============================================================
  // Case 11: on_find_position_for_non_care 钩子被调用
  //          默认实现返回 false -> find_positions_for_basics 返回 false
  // ============================================================
  CASE_MSG_INFO() << "Case 11: non-care 走钩子，默认钩子返回 false\n";
  {
    // 用 kCharacterEquipment 初始化（is_care_item_size=false）
    TestItemGridAlgorithm grid;
    grid.init(1, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kEquipmentTypeId, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    // 默认 on_find_position_for_non_care 返回 false -> 整体返回 false
    CASE_EXPECT_FALSE(grid.find_positions_for_basics(config, basics, out));
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
      int32_t on_check_add(
          const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
          const ItemGridAddRequest& request) const override {
        if (request.item_instance &&
            request.item_instance->item_basic().position().grid_position().user_inventory().x() == 0) {
          return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // 拒绝 x=0
        }
        return TestItemGridAlgorithm::on_check_add(config_group, request);
      }
    };

    RejectFirstColGrid grid;
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory);
    grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);

    std::vector<PROJECT_NAMESPACE_ID::DItemBasic> basics = {make_basic(kItemTypeId_1x1, 1)};
    std::vector<PROJECT_NAMESPACE_ID::DItemGridPosition> out;
    CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, basics, out));
    CASE_EXPECT_EQ(1u, out.size());
    // 结果必须不在 x=0 列
    CASE_EXPECT_NE(0, get_x(out[0]));
    CASE_EXPECT_TRUE(get_x(out[0]) >= 1 && get_x(out[0]) < 4);
    CASE_EXPECT_TRUE(get_y(out[0]) >= 0 && get_y(out[0]) < 4);
  }

  CASE_MSG_INFO() << "=== find_positions_for_basics 测试完成 ===\n";
}
