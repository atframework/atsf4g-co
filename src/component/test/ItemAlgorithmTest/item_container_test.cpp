// Copyright 2025 atframework

#include <frame/test_macros.h>

#include <ItemAlgorithm/Container/ItemFiniteGridContainer.h>
#include <ItemAlgorithm/Container/ItemInfiniteGridContainer.h>
#include <ItemAlgorithm/Container/ItemNoPositionContainer.h>
#include <ItemAlgorithm/ItemContainer.h>
#include <ItemAlgorithm/ItemContainerGroup.h>
#include <ItemAlgorithm/ItemContainerTypes.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
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
// 测试夹具 — 用测试内注册表替代配置组查询
//
// 实现已从"单类 + mode_"拆成 ItemContainer 基类 + 三种模式容器,
// 因此这里用模板把公共测试辅助 (register_position_cfg / create_empty_clone) 混入具体容器类型,
// 位置配置查询钩子只混入格子模式 (无位置模式不查位置配置)。
// ============================================================

ITEM_ALGORITHM_NAMESPACE_BEGIN
namespace item_algorithm {

/// @brief 位置字段映射 (测试接入层实现): 按 init 声明的位置字段读写坐标
///
/// 组件不映射 proto 位置字段, extract_position / apply_position 由接入层实现;
/// 这里覆盖测试用到的四种: user_inventory / character_inventory / character_equipment / virtual_inventory。
/// 装备槽用 slot_idx 作为 x, virtual_inventory 不落位 (坐标恒为 0)。
template <typename ContainerT>
class TestContainerWithPositionMapping : public ContainerT {
 protected:
  item_algorithm::ItemGridPosition extract_position(
      const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const override {
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
        result.x = static_cast<int32_t>(position.character_equipment().slot_idx());
        result.y = 0;
        break;
      case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory:
        break;
      default:
        break;
    }
    return result;
  }

  void apply_position(PROJECT_NAMESPACE_ID::DItemGridPosition& position,
                      const item_algorithm::ItemGridPosition& grid_pos) const override {
    switch (this->get_position_type()) {
      case PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory: {
        auto* out = position.mutable_user_inventory();
        out->set_x(grid_pos.x);
        out->set_y(grid_pos.y);
        break;
      }
      case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory: {
        auto* out = position.mutable_character_inventory();
        out->set_x(grid_pos.x);
        out->set_y(grid_pos.y);
        break;
      }
      case PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment:
        position.mutable_character_equipment()->set_slot_idx(
            static_cast<PROJECT_NAMESPACE_ID::EnEquipmentSlot>(grid_pos.x));
        break;
      case PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory:
        // virtual_inventory 在 proto 里是 bool (只标记归属, 不落位)
        position.set_virtual_inventory(true);
        break;
      default:
        break;
    }
  }
};

/// @brief 位置配置注册表 (测试用): 按 type_id 提供 accumulation_limit / row_size / column_size
///
/// 注册表使测试不依赖配置表 (config_group) 的真实内容。
/// create_empty_clone 会复制注册表, 供 check_replace 复用 check_add 的校验。
template <typename ContainerT>
class TestContainerWithPositionRegistry : public TestContainerWithPositionMapping<ContainerT> {
 public:
  virtual ~TestContainerWithPositionRegistry() = default;

  /// @brief 注册 type_id → DItemPositionCfg 的映射 (替代配置表查询)
  void register_position_cfg(int32_t type_id, int32_t accumulation_limit, int32_t row_size, int32_t col_size) {
    auto& cfg = position_cfg_map_[type_id];
    cfg.set_accumulation_limit(accumulation_limit);
    cfg.set_row_size(row_size);
    cfg.set_column_size(col_size);
  }

 protected:
  /// @brief 查注册表 (格子模式用它覆盖 get_item_position_cfg)
  const PROJECT_NAMESPACE_ID::DItemPositionCfg* lookup_position_cfg(int32_t type_id) const {
    auto it = position_cfg_map_.find(type_id);
    if (it != position_cfg_map_.end()) {
      return &it->second;
    }
    return nullptr;
  }

 protected:
  std::unordered_map<int32_t, PROJECT_NAMESPACE_ID::DItemPositionCfg> position_cfg_map_;
};

/// @brief 格子模式夹具 (有限/无限格子): 位置配置来自测试注册表
///
/// 空克隆会复制注册表, 因此 check_replace 复用的 check_add 校验与本体一致。
/// 无位置模式不查位置配置, 直接用 TestContainerWithPositionRegistry。
template <typename ContainerT>
class TestContainerWithPositionCfg : public TestContainerWithPositionRegistry<ContainerT> {
 protected:
  item_container_ptr_t create_empty_clone() const override {
    auto container = atfw::util::memory::make_strong_rc<TestContainerWithPositionCfg<ContainerT>>();
    // 基类依赖模板参数, 需要显式 this-> 才能查到 copy_empty_config_to
    this->copy_empty_config_to(*container);
    container->position_cfg_map_ = this->position_cfg_map_;
    return container;
  }

  const PROJECT_NAMESPACE_ID::DItemPositionCfg* get_item_position_cfg(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
      const PROJECT_NAMESPACE_ID::DItemBasic& basic) const override {
    return this->lookup_position_cfg(basic.type_id());
  }
};

/// @brief 有限格子模式夹具 (位置配置来自测试注册表)
using TestItemFiniteGridContainer = TestContainerWithPositionCfg<ItemFiniteGridContainer>;

/// @brief 无限格子模式夹具 (装备槽这类: 每件道具占一个位置, 不限行列)
using TestItemInfiniteGridContainer = TestContainerWithPositionCfg<ItemInfiniteGridContainer>;

/// @brief 无位置模式夹具 (只按类型记录数量, 不涉及位置配置)
class TestItemNoPositionContainer : public TestContainerWithPositionRegistry<ItemNoPositionContainer> {
 protected:
  /// @brief 空克隆: 无位置模式不查位置配置, 返回同类型空容器即可
  item_container_ptr_t create_empty_clone() const override {
    auto container = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
    this->copy_empty_config_to(*container);
    return container;
  }
};

/// @brief 无限格子模式夹具: 装备槽, 固定落到构造时指定的槽位
class TestItemInfiniteEquipmentSlotContainer : public TestItemInfiniteGridContainer {
 public:
  explicit TestItemInfiniteEquipmentSlotContainer(int32_t slot_idx = 1) : slot_idx_(slot_idx) {}

 protected:
  bool on_find_position_for_infinite(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
      const PROJECT_NAMESPACE_ID::DItemBasic& /*basic*/,
      PROJECT_NAMESPACE_ID::DItemGridPosition& out_pos) const override {
    out_pos.mutable_character_equipment()->set_slot_idx(static_cast<PROJECT_NAMESPACE_ID::EnEquipmentSlot>(slot_idx_));
    return true;
  }

 private:
  int32_t slot_idx_ = 1;
};

struct HookTestState {
  bool reject_all_add = false;
  bool reject_sub = false;
  bool reject_count_limit = false;
  int32_t rejected_position_x = -1;
  int32_t rejected_add_position_x = -1;
  int32_t check_item_position_calls = 0;
  int32_t on_check_add_calls = 0;
  int32_t on_check_sub_calls = 0;
  int32_t on_check_item_count_limit_calls = 0;
  // 最近一次 on_item_count_changed 收到的操作上下文 (用干验证调用方传入的操作来源确实透传到钩子)
  int32_t count_changed_calls = 0;
  item_algorithm::ItemOperationContext last_count_context;

  void reset_call_counts() {
    check_item_position_calls = 0;
    on_check_add_calls = 0;
    on_check_sub_calls = 0;
    on_check_item_count_limit_calls = 0;
  }
};

class HookTestItemFiniteGridContainer : public TestItemFiniteGridContainer {
 public:
  HookTestItemFiniteGridContainer() : state_(std::make_shared<HookTestState>()) {}
  explicit HookTestItemFiniteGridContainer(std::shared_ptr<HookTestState> state) : state_(std::move(state)) {}

  HookTestState& hook_state() { return *state_; }

 protected:
  item_container_ptr_t create_empty_clone() const override {
    auto grid = atfw::util::memory::make_strong_rc<HookTestItemFiniteGridContainer>(state_);
    copy_empty_config_to(*grid);
    grid->register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    grid->register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
    return grid;
  }

  int32_t on_check_add_one(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                           const PROJECT_NAMESPACE_ID::DItemInstance& request) const override {
    ++state_->on_check_add_calls;
    if (state_->reject_all_add ||
        (state_->rejected_add_position_x >= 0 &&
         get_inventory_x(request.item_basic().position()) == state_->rejected_add_position_x)) {
      return kHookRejectedError;
    }
    return TestItemFiniteGridContainer::on_check_add_one(config_group, request);
  }

  int32_t on_check_sub_one(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
                           const PROJECT_NAMESPACE_ID::DItemBasic& request) const override {
    ++state_->on_check_sub_calls;
    if (state_->reject_sub) {
      return kHookRejectedError;
    }
    return TestItemFiniteGridContainer::on_check_sub_one(config_group, request);
  }

  int32_t on_check_item_count_limit(int32_t type_id, int64_t current_count, int64_t add_count) const override {
    ++state_->on_check_item_count_limit_calls;
    if (state_->reject_count_limit) {
      return kHookRejectedError;
    }
    return TestItemFiniteGridContainer::on_check_item_count_limit(type_id, current_count, add_count);
  }

  bool check_item_position(const PROJECT_NAMESPACE_ID::DItemPosition& position) const override {
    ++state_->check_item_position_calls;
    return state_->rejected_position_x < 0 || get_inventory_x(position) != state_->rejected_position_x;
  }

  void on_item_count_changed(int32_t type_id, const item_algorithm::item_entry_ptr_t& entry, int64_t guid,
                             const item_algorithm::ItemGridPosition& position, int64_t old_count, int64_t new_count,
                             int64_t type_total_count, const item_algorithm::ItemOperationContext& context) override {
    ++state_->count_changed_calls;
    state_->last_count_context = context;
    TestItemFiniteGridContainer::on_item_count_changed(type_id, entry, guid, position, old_count, new_count,
                                                       type_total_count, context);
  }

 private:
  static int32_t get_inventory_x(const PROJECT_NAMESPACE_ID::DItemPosition& position) {
    if (position.grid_position().has_user_inventory()) {
      return position.grid_position().user_inventory().x();
    }
    return -1;
  }

 private:
  std::shared_ptr<HookTestState> state_;
};

/// @brief 服务器端测试子类 — 在 on_item_data_changed 中只记录 entry_id
///
/// 用于模拟服务器操作 → 收集变更 → 同步到客户端子类 的完整流程。
/// collect_apply_data() 提取时实时通过 find_entry_by_id() 获取 Entry,
/// 从而保证同步的是最新数据 (如 mutable_item_data 的修改)。
class ServerTestItemFiniteGridContainer : public TestItemFiniteGridContainer {
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
  void on_item_data_changed(const item_entry_ptr_t& entry, const ItemOperationContext& /*context*/) override {
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

// ============================================================
// 请求容器 — 自持数据, 同时自己就是只读 iterable
//
// 组件里的 checked request 只持有传入的视图 (不复制数据), 所以传给 check_* 的视图必须活到
// add / sub / replace 执行完。测试里用这个类型代替原来的 ItemAddRequest 等别名:
// 数据就在自己身上, 它自己就是视图, 生命周期天然覆盖后续执行阶段。
// ============================================================
template <class T>
class TestItemRequestList : public ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::item_iterable<T, false> {
 public:
  using base_type = ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::item_iterable<T, false>;
  using callback_parameter = typename base_type::callback_parameter;
  using storage_type = ::google::protobuf::RepeatedPtrField<T>;

  TestItemRequestList() = default;
  TestItemRequestList(const TestItemRequestList&) = default;
  TestItemRequestList& operator=(const TestItemRequestList&) = default;

  T* Add() { return storage_.Add(); }
  T& operator[](int index) { return storage_[index]; }
  const T& operator[](int index) const { return storage_[index]; }
  void Clear() { storage_.Clear(); }
  storage_type& storage() { return storage_; }
  const storage_type& storage() const { return storage_; }

  bool empty() const noexcept override { return storage_.empty(); }
  size_t size() const noexcept override { return static_cast<size_t>(storage_.size()); }

  bool foreach (atfw::util::nostd::function_ref<bool(callback_parameter)> callback) const override {
    for (const auto& item : storage_) {
      if (!callback(item)) {
        return false;
      }
    }
    return true;
  }
  using base_type::foreach;

 private:
  storage_type storage_;
};

using ItemAddRequest = TestItemRequestList<PROJECT_NAMESPACE_ID::DItemInstance>;
using ItemSubRequest = TestItemRequestList<PROJECT_NAMESPACE_ID::DItemBasic>;
using ItemReplaceRequest = TestItemRequestList<PROJECT_NAMESPACE_ID::DItemInstance>;
using ItemHasRequest = TestItemRequestList<PROJECT_NAMESPACE_ID::DItemBasic>;

/// @brief check_* 只持有视图引用, 所以这里返回容器自身的引用 (它比 checked request 活得久);
///        容器没有 begin/end, 组件里返回临时视图的那个模板会被 SFINAE 掉, 不会选中。
template <class T>
TestItemRequestList<T>& make_item_readable_iterable(TestItemRequestList<T>& requests) {
  return requests;
}
template <class T>
const TestItemRequestList<T>& make_item_readable_iterable(const TestItemRequestList<T>& requests) {
  return requests;
}

// 全局作用域直接使用组件类型 (ItemContainer, ItemEntry, item_entry_ptr_t 等) 与 item_algorithm:: 前缀,
// 这里必须用 using-directive, 无法改成 using-declaration。
using namespace ITEM_ALGORITHM_NAMESPACE_ID;                  // NOLINT(build/namespaces)
using namespace ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm;  // NOLINT(build/namespaces)

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

/// @brief 日志视图取数据指针 (空视图用空串, 保证能直接传给 printf 的 %.*s)
static const char* log_view_data(const gsl::string_view& view) { return view.empty() ? "" : view.data(); }

/// @brief 日志视图长度 (%.*s 的精度参数)
static int log_view_size(const gsl::string_view& view) { return static_cast<int>(view.size()); }

/// @brief 测试日志处理器: 默认只输出 Info 及以上到终端, Error/Warning 着重显示
///
///  - Debug 日志不输出 (默认只打印 Info 及以上)
///  - Error / Warning 输出到 stderr 并着色加粗
///  - Info 输出到 stdout
static void test_item_log_handler(const ItemLogRecord& record) {
  if (record.level < ItemLogLevel::kWarning) {
    return;
  }

  // 记录的文本字段是视图 (不保证 NUL 结尾), 用 %.*s 按长度输出
  switch (record.level) {
    case ItemLogLevel::kError: {
      fprintf(stderr, "\033[1;31m[ERROR] %.*s:%u [%.*s] %.*s\033[0m\n", log_view_size(record.file_path),
              log_view_data(record.file_path), record.line_number, log_view_size(record.category),
              log_view_data(record.category), log_view_size(record.message), log_view_data(record.message));
      break;
    }
    case ItemLogLevel::kWarning: {
      fprintf(stderr, "\033[1;33m[WARNING] %.*s:%u [%.*s] %.*s\033[0m\n", log_view_size(record.file_path),
              log_view_data(record.file_path), record.line_number, log_view_size(record.category),
              log_view_data(record.category), log_view_size(record.message), log_view_data(record.message));
      break;
    }
    default: {
      fprintf(stdout, "[INFO] %.*s:%u [%.*s] %.*s\n", log_view_size(record.file_path), log_view_data(record.file_path),
              record.line_number, log_view_size(record.category), log_view_data(record.category),
              log_view_size(record.message), log_view_data(record.message));
      break;
    }
  }
}

/// @brief 为测试容器注册日志处理器 (默认只输出 Info 及以上)
static void register_test_log_handler(ItemContainer& grid) {
  ItemLogHandler handler;
  handler.category = "ItemAlgorithm";
  handler.on_log = test_item_log_handler;
  grid.set_log_handler(handler);
}

/// @brief 初始化测试容器 (inventory 类型, 默认 10x10)
static void init_test_container(TestItemFiniteGridContainer& grid, int32_t row = 10, int32_t col = 10,
                                int64_t container_guid = 0) {
  grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, container_guid);
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

/// @brief 通过 foreach_instance (Dump 接口) 导出容器中所有条目
static std::vector<PROJECT_NAMESPACE_ID::DItemInstance> dump_container_items(const ItemContainer& grid) {
  std::vector<PROJECT_NAMESPACE_ID::DItemInstance> items;
  grid.foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
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

/// @brief 校验 get_item_count_debug() 与 get_item_count() 缓存计数一致
///
/// 在通过 get_item_count_debug() 判断数量时, 额外校验缓存计数与实时计数相等
static void verify_item_count_consistency(const ItemContainer& grid, int32_t type_id) {
  int64_t real_count = grid.get_item_count_debug(type_id);
  int64_t cached_count = grid.get_item_count(type_id);
  CASE_EXPECT_EQ(cached_count, real_count);
  if (cached_count != real_count) {
    CASE_MSG_ERROR() << "type_id=" << type_id << " real_count=" << real_count << " cached_count=" << cached_count;
  }
}

/// @brief 校验 find_entry_by_id 能找到指定 entry, 且数据符合预期
static void verify_find_entry_by_id(const ItemContainer& grid, uint64_t entry_id, int32_t type_id, int64_t count,
                                    int64_t guid) {
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
static void verify_not_find_entry_by_id(const ItemContainer& grid, uint64_t entry_id) {
  auto found = grid.find_entry_by_id(entry_id);
  CASE_EXPECT_TRUE(found == nullptr);
  if (found) {
    CASE_MSG_ERROR() << "entry_id " << entry_id << " should be removed but still found";
  }
}

/// @brief 通过 foreach_instance (Dump 接口) 遍历容器中所有条目, 验证数据一致性
///
/// 校验内容:
///   1. 每个条目 type_id > 0 且 count > 0
///   2. 占格道具可通过 get(position) 找回, 且字段一致
///   3. 各 type_id 的累计数量与 get_item_count_debug() 一致
///   4. get_all_groups() 中无多余非空组
template <typename ContainerT>
static void verify_container_dump(const ContainerT& grid) {
  std::unordered_map<int32_t, int64_t> type_counts;
  int total_entries = 0;

  grid.foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
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

  // 各类型累计数量 == get_item_count_debug(), 且缓存计数一致
  for (const auto& pair : type_counts) {
    CASE_EXPECT_EQ(grid.get_item_count_debug(pair.first), pair.second);
    verify_item_count_consistency(grid, pair.first);
  }

  // 反向: get_all_groups() 中出现的非空组必须在 foreach_instance 中被统计到
  for (const auto& group_pair : grid.get_all_groups()) {
    int64_t foreach_count = 0;
    auto it = type_counts.find(group_pair.first);
    if (it != type_counts.end()) {
      foreach_count = it->second;
    }
    int64_t api_count = grid.get_item_count_debug(group_pair.first);
    CASE_EXPECT_EQ(foreach_count, api_count);
    verify_item_count_consistency(grid, group_pair.first);
  }

  // find_entry_by_id 一致性: 所有现存 entry 都应能通过 entry_id 找到, 且数据一致
  for (const auto& group_pair : grid.get_all_groups()) {
    for (const auto& entry : group_pair.second.entries) {
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

// ============================================================
// 状态快照 — 用于断言"失败 / 只读操作不得修改容器"
// ============================================================

/// @brief 容器对外可观测状态
///
/// 比较操作前后的: 实时数量 / 缓存数量 / 分组条目数 / entry_id 集合 / 占格位图。
/// 缓存单独记录, 这样"分组已空但缓存还有值"的泄漏也能被发现。
struct ContainerStateSnapshot {
  std::map<int32_t, int64_t> real_counts;
  std::map<int32_t, int64_t> cached_counts;
  std::map<int32_t, size_t> group_sizes;
  std::vector<uint64_t> entry_ids;
  std::vector<int32_t> occupied_cells;  ///< 位图占用格, 编码为 row * column_count + column
};

/// @param tracked_type_ids 额外读取缓存的类型 (用于发现分组为空但缓存非 0 的泄漏)
template <typename ContainerT>
static ContainerStateSnapshot capture_container_state(const ContainerT& container,
                                                      const std::vector<int32_t>& tracked_type_ids) {
  ContainerStateSnapshot snapshot;

  for (const auto& group_pair : container.get_all_groups()) {
    snapshot.group_sizes[group_pair.first] = group_pair.second.entries.size();
    snapshot.real_counts[group_pair.first] = container.get_item_count_debug(group_pair.first);
    for (const auto& entry : group_pair.second.entries) {
      if (entry) {
        snapshot.entry_ids.push_back(entry->entry_id());
      }
    }
  }
  std::sort(snapshot.entry_ids.begin(), snapshot.entry_ids.end());

  for (int32_t type_id : tracked_type_ids) {
    snapshot.cached_counts[type_id] = container.get_item_count(type_id);
  }

  const auto& flags = container.get_occupy_grid_flag();
  for (size_t r = 0; r < flags.row_count(); ++r) {
    for (size_t c = 0; c < flags.column_count(); ++c) {
      if (flags.is_occupied(static_cast<int32_t>(c), static_cast<int32_t>(r))) {
        snapshot.occupied_cells.push_back(static_cast<int32_t>(r * flags.column_count() + c));
      }
    }
  }

  return snapshot;
}

static bool container_state_equal(const ContainerStateSnapshot& lhs, const ContainerStateSnapshot& rhs) {
  return lhs.real_counts == rhs.real_counts && lhs.cached_counts == rhs.cached_counts &&
         lhs.group_sizes == rhs.group_sizes && lhs.entry_ids == rhs.entry_ids &&
         lhs.occupied_cells == rhs.occupied_cells;
}

/// @brief 设置道具实例的容器归属 (新增用例统一使用非 0 容器 GUID, 避免默认值让实现侥幸通过)
static void set_item_container_guid(PROJECT_NAMESPACE_ID::DItemInstance& instance, int64_t container_guid) {
  instance.mutable_item_basic()->mutable_position()->set_container_guid(container_guid);
}

/// @brief 设置扣减/查询请求的容器归属
static void set_basic_container_guid(PROJECT_NAMESPACE_ID::DItemBasic& basic, int64_t container_guid) {
  basic.mutable_position()->set_container_guid(container_guid);
}

/// @brief 辅助: 将 std::vector 形式的参数转换为 protobuf 类型, 并调用 apply_entries
template <typename ContainerT>
static void call_apply_entries(
    ContainerT& grid, const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
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
  grid.apply_entries(config_group, pb_remove_ids, make_item_readable_iterable(pb_updates));
}

/// @brief 初始化 ServerTestItemFiniteGridContainer (与 init_test_container 相同配置)
static void init_server_container(ServerTestItemFiniteGridContainer& grid, int32_t row = 10, int32_t col = 10) {
  grid.init(row, col, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
  // 注册测试日志处理器 (默认只输出 Info 及以上)
  register_test_log_handler(grid);
}

/// @brief 服务器子类完成操作后, 将变更同步到客户端子类, 并验证双方数据一致
///
/// 步骤:
///   1. 用 ServerTestItemFiniteGridContainer::collect_apply_data() 生成同步参数
///   2. 在 client 上执行 apply_entries
///   3. 对比 server 与 client 的:
///      a. 各 type_id 数量 (get_item_count_debug)
///      b. 所有 entry (按 entry_id 逐条对比 type_id / count / guid)
///      c. 占格标记 (get_occupy_grid_flag)
///      d. 双方 verify_container_dump
static void verify_server_client_sync(const ServerTestItemFiniteGridContainer& server,
                                      const TestItemFiniteGridContainer& client) {
  // 1. 比较 item_count_cache — 收集所有出现的 type_id
  const auto& server_groups = server.get_all_groups();
  const auto& client_groups = client.get_all_groups();

  std::set<int32_t> all_type_ids;
  for (const auto& pair : server_groups) {
    if (!pair.second.entries.empty()) {
      all_type_ids.insert(pair.first);
    }
  }
  for (const auto& pair : client_groups) {
    if (!pair.second.entries.empty()) {
      all_type_ids.insert(pair.first);
    }
  }

  for (int32_t tid : all_type_ids) {
    CASE_EXPECT_EQ(server.get_item_count_debug(tid), client.get_item_count_debug(tid));
    verify_item_count_consistency(server, tid);
    verify_item_count_consistency(client, tid);
  }

  // 2. 按 entry_id 逐条对比
  std::unordered_map<uint64_t, const ItemEntry*> server_entries, client_entries;
  for (const auto& group_pair : server_groups) {
    for (const auto& entry : group_pair.second.entries) {
      if (entry) {
        server_entries[entry->entry_id()] = entry.get();
      }
    }
  }
  for (const auto& group_pair : client_groups) {
    for (const auto& entry : group_pair.second.entries) {
      if (entry) {
        client_entries[entry->entry_id()] = entry.get();
      }
    }
  }

  CASE_EXPECT_EQ(server_entries.size(), client_entries.size());

  for (const auto& pair : server_entries) {
    uint64_t eid = pair.first;
    const ItemEntry* s_entry = pair.second;
    auto it = client_entries.find(eid);
    if (it == client_entries.end()) {
      CASE_MSG_ERROR() << "entry_id " << eid << " exists in server but not in client";
      continue;
    }
    const ItemEntry* c_entry = it->second;
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

  // 4. 双方各自 verify_container_dump
  verify_container_dump(server);
  verify_container_dump(client);
}

/// @brief 每一步服务器操作后: 收集变更 → 同步到客户端 → 验证双方一致
///
/// @param server  服务器容器 (已完成操作)
/// @param client  客户端容器 (每步增量同步)
/// @param config  共享 config_group
/// @param step_name 当前步骤描述 (用于错误信息定位)
static void sync_and_verify(ServerTestItemFiniteGridContainer& server, TestItemFiniteGridContainer& client,
                            const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config,
                            const char* step_name) {
  ::google::protobuf::RepeatedField<uint64_t> remove_ids;
  ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstanceEntry> update_entries;
  server.collect_apply_data(remove_ids, update_entries);
  server.clear_change_cache();

  client.apply_entries(config, remove_ids, make_item_readable_iterable(update_entries));

  // 验证双方一致
  verify_server_client_sync(server, client);

  // 额外: 打印步骤信息便于定位
  (void)step_name;
}

// ============================================================
// 完整生命周期大测试 — 模拟一个玩家从新手奖励到客户端同步的全过程
//
// 覆盖操作: check_add / add (1x1/2x2/装备GUID),
//           有位置容器对无位置道具的拒绝 / stack overflow / position occupied / out of range,
//           check_sub / sub (部分/全部/按位置/按GUID/不足失败),
//           check_move / move (整体/部分拆分/目标占用失败),
//           load (占格/装备), foreach, clear,
//           entry_id (自增/独立/拆分产生新条目),
//           apply_entries (删除/更新/新增/位置变更/装备GUID),
//           具体容器的单容器 Move 与显式跨容器 sub/add,
//           服务器→客户端同步验证
// ============================================================

CASE_TEST(ItemContainer, lifecycle_and_client_sync) {
  auto server_ptr = atfw::util::memory::make_strong_rc<ServerTestItemFiniteGridContainer>();
  auto& server = *server_ptr;
  init_server_container(server);
  // 同时注册装备配置, 使服务器支持所有道具类型
  server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto client_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& client = *client_ptr;
  init_test_container(client);
  client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto config = make_test_config_group();

  // ----------------------------------------------------------------
  // Step 1: 新手奖励 — 添加初始装备 (guid=1001)
  // 验证: check_add 通过, add 成功, Dump/Sync 正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 1: 新手奖励 ===\n";
  {
    auto equip = make_equip_item(1001, 0, 0);
    ItemAddRequest reqs;
    *reqs.Add() = equip;

    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.add(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kEquipmentTypeId), 1);
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1001) != nullptr);
  CASE_EXPECT_FALSE(server.is_empty());
  // entry_id 应从 1 开始自增
  CASE_EXPECT_EQ(server.peek_next_entry_id(), static_cast<uint64_t>(2));
  {
    auto dumped = dump_container_items(server);
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
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 1: 新手奖励");

  // ----------------------------------------------------------------
  // Step 2: 拾取材料 — 添加 1x1 道具到 (1,0) count=30, (2,0) count=50
  // 验证: 批量 add, 占格标记正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 2: 拾取材料 ===\n";
  {
    auto item1 = make_grid_item(kItemTypeId_1x1, 30, 1, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 50, 2, 0);
    ItemAddRequest reqs;
    *reqs.Add() = item1;
    *reqs.Add() = item2;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 80);
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
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 2: 拾取材料");

  // ----------------------------------------------------------------
  // Step 3: 有位置 Grid 不允许添加无位置道具
  // 验证: 新模式约束拒绝普通道具，现有占格数据不变
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 3: 有位置 Grid 拒绝无位置道具 ===\n";
  {
    ItemAddRequest reqs;
    *reqs.Add() = make_ungrid_item(kCoinTypeId, 200);
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    CASE_EXPECT_EQ(server.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kCoinTypeId), 0);
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 3: 有位置 Grid 拒绝无位置道具");

  // ----------------------------------------------------------------
  // Step 4: 1x1 堆叠追加 — 在 (1,0) 追加 60 个 (已有 30, 合计 90, 上限 99)
  // 验证: 占格道具合并堆叠
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 4: 1x1 堆叠追加 ===\n";
  {
    auto item = make_grid_item(kItemTypeId_1x1, 60, 1, 0);
    ItemAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 140);  // 90 + 50
  verify_item_count_consistency(server, kItemTypeId_1x1);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos10;
    gpos10.mutable_user_inventory()->set_x(1);
    gpos10.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos10);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) CASE_EXPECT_EQ(e->item_instance().item_basic().count(), static_cast<int64_t>(90));
  }
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 4: 1x1 堆叠追加");

  // ----------------------------------------------------------------
  // Step 5: check_add 失败 — 堆叠溢出 / 位置占用 / 超出边界
  // 验证: 各种失败 error_code, 服务器数据不变
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 5: check_add 失败检查 ===\n";
  // 5a: stack overflow — (1,0) 已有 90, 再加 10 = 100 > 99
  {
    auto item = make_grid_item(kItemTypeId_1x1, 10, 1, 0);
    ItemAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
    // add 应直接返回错误
    auto result = server.add(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
  }
  // 5b: position occupied — (0,0) 已有装备
  {
    auto item = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    ItemAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 5c: out of range — (10,10) 超出 10x10 背包
  {
    auto item = make_grid_item(kItemTypeId_1x1, 1, 10, 10);
    ItemAddRequest reqs;
    *reqs.Add() = item;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据应不变
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 140);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  verify_container_dump(server);

  // ----------------------------------------------------------------
  // Step 6: 装备 GUID 相关失败检查
  // 验证: guid=0 失败, 重复 GUID 失败, 位置已被占用失败
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 6: 装备 GUID 添加失败检查 ===\n";
  // 6a: guid=0 应失败
  {
    auto bad = make_grid_item(kEquipmentTypeId, 1, 3, 0, 0);  // guid=0
    ItemAddRequest reqs;
    *reqs.Add() = bad;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 6b: 重复 GUID=1001 应失败
  {
    auto dup = make_equip_item(1001, 3, 0);  // GUID 1001 已存在
    ItemAddRequest reqs;
    *reqs.Add() = dup;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 6c: 位置 (0,0) 被装备 1001 占用
  {
    auto conflict = make_equip_item(9999, 0, 0);  // 新GUID但位置冲突
    ItemAddRequest reqs;
    *reqs.Add() = conflict;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  verify_container_dump(server);

  // ----------------------------------------------------------------
  // Step 7: 添加 2x2 大物品 — 在 (4,4) 放一个 2x2 (accumulation_limit=1)
  // 验证: 2x2 占格标记正确 (4 格)
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 7: 添加 2x2 大物品 ===\n";
  {
    auto big = make_grid_item(kItemTypeId_2x2, 1, 4, 4);
    ItemAddRequest reqs;
    *reqs.Add() = big;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_2x2), 1);
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
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 7: 添加 2x2 大物品");

  // ----------------------------------------------------------------
  // Step 8: 获取更多装备 — guid=1002 在 (3,0), guid=1003 在 (4,0)
  // 验证: 多件装备, GUID 索引正确
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 8: 获取更多装备 ===\n";
  {
    auto e2 = make_equip_item(1002, 3, 0);
    auto e3 = make_equip_item(1003, 4, 0);
    ItemAddRequest reqs;
    *reqs.Add() = e2;
    *reqs.Add() = e3;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kEquipmentTypeId), 3);
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1002) != nullptr);
  CASE_EXPECT_TRUE(server.get_by_guid(1003) != nullptr);
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 8: 获取更多装备");

  // ----------------------------------------------------------------
  // Step 9: 消费材料 — 扣减 (2,0) 上的 1x1 道具 20 个 (50→30)
  // 验证: check_sub 通过, 部分扣减, 不释放位置
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 9: 消费材料 (部分扣减) ===\n";
  {
    auto sub = make_sub_basic(kItemTypeId_1x1, 20, 2, 0);
    ItemSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = server.sub(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 120);  // 90 + 30
  verify_item_count_consistency(server, kItemTypeId_1x1);
  {
    PROJECT_NAMESPACE_ID::DItemGridPosition gpos20;
    gpos20.mutable_user_inventory()->set_x(2);
    gpos20.mutable_user_inventory()->set_y(0);
    auto e = server.get(gpos20);
    CASE_EXPECT_TRUE(e != nullptr);
    if (e) CASE_EXPECT_EQ(e->item_instance().item_basic().count(), static_cast<int64_t>(30));
  }
  verify_container_dump(server);
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
    ItemSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    server.sub(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 90);  // 只剩 (1,0)=90
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
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 10: 消费材料 (完全扣减)");

  // ----------------------------------------------------------------
  // Step 11: 扣减失败检查 — 数量不足 / 装备 GUID 不匹配
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 11: check_sub 失败检查 ===\n";
  // 11a: 扣减不足
  {
    auto sub = make_sub_basic(kItemTypeId_1x1, 999, 1, 0);
    ItemSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    // sub 应直接返回错误
    auto result = server.sub(checked);
    CASE_EXPECT_NE(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11b: 装备 guid=0 失败
  {
    auto sub = make_sub_basic(kEquipmentTypeId, 1, 0, 0, 0);
    ItemSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11c: 装备不存在的 GUID
  {
    auto sub = make_equip_sub_by_guid(77777);
    ItemSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 11d: 装备 count != 1
  {
    PROJECT_NAMESPACE_ID::DItemBasic bad_sub;
    bad_sub.set_type_id(kEquipmentTypeId);
    bad_sub.set_count(2);
    bad_sub.set_guid(1001);
    ItemSubRequest reqs;
    *reqs.Add() = bad_sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 90);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  CASE_EXPECT_EQ(server.get_item_count_debug(kEquipmentTypeId), 3);
  verify_item_count_consistency(server, kEquipmentTypeId);
  verify_container_dump(server);

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
    ItemSubRequest reqs;
    *reqs.Add() = sub;
    auto checked = server.check_sub(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.sub(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kEquipmentTypeId), 2);
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
  verify_container_dump(server);
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
      ItemMoveRequest move_req;
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
    CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 90);
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
    verify_container_dump(server);
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

    ItemMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 40});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(5);
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 40});

    auto checked = server.check_move(config, std::move(move_req));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.move(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 90);  // 总量不变
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
  verify_container_dump(server);
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

    ItemMoveRequest move_req;
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

    ItemMoveRequest move_req;
    move_req.move_sub_entrys.push_back({entry, 10});

    PROJECT_NAMESPACE_ID::DItemPosition goal;
    goal.mutable_grid_position()->mutable_user_inventory()->set_x(0);  // (0,0) 是装备
    goal.mutable_grid_position()->mutable_user_inventory()->set_y(0);
    move_req.move_add_entrys.push_back({entry, goal, 10});

    auto checked = server.check_move(config, std::move(move_req));
    CASE_EXPECT_NE(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 90);
  verify_item_count_consistency(server, kItemTypeId_1x1);
  verify_container_dump(server);

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

    ItemMoveRequest move_req;
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
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 16: Move 装备");

  // ----------------------------------------------------------------
  // ----------------------------------------------------------------
  // Step 18: Load 接口 — 模拟从数据库加载存档
  // 新建一个独立的 server/client Grid, Load 占格道具与装备, 验证同步
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 18: Load 从数据库恢复 ===\n";
  {
    // 用独立 Grid 测试 Load
    auto load_server_ptr = atfw::util::memory::make_strong_rc<ServerTestItemFiniteGridContainer>();
    auto& load_server = *load_server_ptr;
    init_server_container(load_server);
    load_server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    auto load_client_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& load_client = *load_client_ptr;
    init_test_container(load_client);
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

    CASE_EXPECT_EQ(load_server.get_item_count_debug(kItemTypeId_1x1), 65);  // 55+10 堆叠
    verify_item_count_consistency(load_server, kItemTypeId_1x1);
    CASE_EXPECT_EQ(load_server.get_item_count_debug(kEquipmentTypeId), 1);
    verify_item_count_consistency(load_server, kEquipmentTypeId);
    CASE_EXPECT_TRUE(load_server.get_by_guid(2001) != nullptr);
    // find_entry_by_id 校验: Load 的条目都应能按 entry_id 找到
    {
      uint64_t item_eid = (*load_server.get_group(kItemTypeId_1x1)->begin())->entry_id();
      uint64_t equip_eid = load_server.get_by_guid(2001)->entry_id();
      verify_find_entry_by_id(load_server, item_eid, kItemTypeId_1x1, 65, 0);
      verify_find_entry_by_id(load_server, equip_eid, kEquipmentTypeId, 1, 2001);
    }

    verify_container_dump(load_server);
    sync_and_verify(load_server, load_client, config, "Step 18: Load");
  }

  // ----------------------------------------------------------------
  // Step 19: foreach + clear — 清空后验证
  // 用独立 Grid 测试
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 19: foreach + clear ===\n";
  {
    auto temp_ptr = atfw::util::memory::make_strong_rc<ServerTestItemFiniteGridContainer>();
    auto& temp = *temp_ptr;
    init_server_container(temp);

    // 添加若干道具
    auto item1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
    ItemAddRequest reqs;
    *reqs.Add() = item1;
    *reqs.Add() = item2;
    auto checked = temp.check_add(config, make_item_readable_iterable(reqs));
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

    // foreach_instance 计数
    int count = 0;
    temp.foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance&) {
      ++count;
      return true;
    });
    CASE_EXPECT_EQ(count, 2);

    // clear
    temp.clear();
    CASE_EXPECT_TRUE(temp.is_empty());
    CASE_EXPECT_EQ(temp.get_item_count_debug(kItemTypeId_1x1), 0);
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
    verify_container_dump(temp);
  }

  // ----------------------------------------------------------------
  // Step 20: entry_id 独立性 — 两个 Grid 各自自增
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 20: entry_id 独立性验证 ===\n";
  {
    auto grid_a_ptr = atfw::util::memory::make_strong_rc<ServerTestItemFiniteGridContainer>();
    auto& grid_a = *grid_a_ptr;
    init_server_container(grid_a);
    auto grid_b_ptr = atfw::util::memory::make_strong_rc<ServerTestItemFiniteGridContainer>();
    auto& grid_b = *grid_b_ptr;
    init_server_container(grid_b);

    auto item_a = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    ItemAddRequest ra;
    *ra.Add() = item_a;
    {
      auto checked = grid_a.check_add(config, make_item_readable_iterable(ra));
      grid_a.add(checked);
    }
    ItemAddRequest rb_from_a;
    *rb_from_a.Add() = item_a;
    {
      auto checked = grid_b.check_add(config, make_item_readable_iterable(rb_from_a));
      grid_b.add(checked);
    }

    auto item_b = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
    ItemAddRequest rb;
    *rb.Add() = item_b;
    auto checked_b = grid_a.check_add(config, make_item_readable_iterable(rb));
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    init_test_container(grid);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    // 添加几个道具
    auto item1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
    auto item2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
    auto item3 = make_grid_item(kItemTypeId_1x1, 30, 2, 0);
    auto equip = make_equip_item(3001, 3, 0);
    ItemAddRequest reqs;
    *reqs.Add() = item1;
    *reqs.Add() = item2;
    *reqs.Add() = item3;
    *reqs.Add() = equip;
    auto result = grid.check_add(config, make_item_readable_iterable(reqs));
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
    CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 20);  // 只剩 eid2
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

    verify_container_dump(grid);
  }

  // ----------------------------------------------------------------
  // Step 22: Container 单 Grid — check_add / add / check_sub / sub
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 22: Container 单 Grid ===\n";
  {
    auto container_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& container = *container_ptr;
    init_test_container(container);

    // add
    auto item = make_grid_item(kItemTypeId_1x1, 5, 1, 1);
    ItemAddRequest add_reqs;
    *add_reqs.Add() = item;
    auto checked_add = container.check_add(config, make_item_readable_iterable(add_reqs));
    CASE_EXPECT_EQ(checked_add.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto add_result = container.add(checked_add);
    CASE_EXPECT_EQ(add_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(container.get_item_count_debug(kItemTypeId_1x1), 5);
    verify_item_count_consistency(container, kItemTypeId_1x1);
    // find_entry_by_id 校验: add 后 entry 应能找到
    uint64_t container_eid = 0;
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition gpos11;
      gpos11.mutable_user_inventory()->set_x(1);
      gpos11.mutable_user_inventory()->set_y(1);
      auto e = container.get(gpos11);
      CASE_EXPECT_TRUE(e != nullptr);
      if (e) {
        container_eid = e->entry_id();
        verify_find_entry_by_id(container, container_eid, kItemTypeId_1x1, 5, 0);
      }
    }

    // sub
    auto sub = make_sub_basic(kItemTypeId_1x1, 3, 1, 1);
    ItemSubRequest sub_reqs;
    *sub_reqs.Add() = sub;
    auto checked_sub = container.check_sub(config, make_item_readable_iterable(sub_reqs));
    CASE_EXPECT_EQ(checked_sub.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto sub_result = container.sub(checked_sub);
    CASE_EXPECT_EQ(sub_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(container.get_item_count_debug(kItemTypeId_1x1), 2);
    verify_item_count_consistency(container, kItemTypeId_1x1);
    // find_entry_by_id 校验: 部分扣减后 entry 仍在, count 更新为 2
    verify_find_entry_by_id(container, container_eid, kItemTypeId_1x1, 2, 0);

    // check_add 失败: stack overflow
    auto overflow = make_grid_item(kItemTypeId_1x1, 100, 1, 1);
    ItemAddRequest fail_reqs;
    *fail_reqs.Add() = overflow;
    auto fail_checked = container.check_add(config, make_item_readable_iterable(fail_reqs));
    CASE_EXPECT_EQ(fail_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
    auto fail_result = container.add(fail_checked);
    CASE_EXPECT_EQ(fail_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);

    // check_sub 失败: 空背包扣减不存在的道具
    auto bad_sub = make_sub_basic(kVirtualTypeId, 10);
    ItemSubRequest fail_sub_reqs;
    *fail_sub_reqs.Add() = bad_sub;
    auto fail_sub_checked = container.check_sub(config, make_item_readable_iterable(fail_sub_reqs));
    CASE_EXPECT_NE(fail_sub_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    verify_container_dump(container);
  }

  // ----------------------------------------------------------------
  // Step 23: Container Move — 同位置跳过 / 合并操作 / 大小物品交换
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 23: Container Move 系列 ===\n";
  {
    auto container_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& container = *container_ptr;
    init_test_container(container);

    // 放两个 1x1：(0,0)=10, (1,0)=20
    {
      auto i1 = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
      auto i2 = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
      ItemAddRequest reqs;
      *reqs.Add() = i1;
      *reqs.Add() = i2;
      auto checked = container.check_add(config, make_item_readable_iterable(reqs));
      container.add(checked);
    }

    // 23a: same position skip
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition gpos00;
      gpos00.mutable_user_inventory()->set_x(0);
      gpos00.mutable_user_inventory()->set_y(0);
      auto entry = container.get(gpos00);
      ItemMoveRequest move_req;
      move_req.move_sub_entrys.push_back({entry, 5});
      move_req.move_add_entrys.push_back({entry, make_inventory_target(0, 0), 5});
      auto checked = container.check_move(config, std::move(move_req));
      CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      auto result = container.move(checked);
      CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      CASE_EXPECT_EQ(container.get_item_count_debug(kItemTypeId_1x1), 30);  // 不变
      verify_item_count_consistency(container, kItemTypeId_1x1);
    }

    // 23b: 从同一源分两次移动到不同位置
    {
      PROJECT_NAMESPACE_ID::DItemGridPosition gpos00;
      gpos00.mutable_user_inventory()->set_x(0);
      gpos00.mutable_user_inventory()->set_y(0);
      auto entry = container.get(gpos00);
      ItemMoveRequest first_move;
      first_move.move_sub_entrys.push_back({entry, 3});
      first_move.move_add_entrys.push_back({entry, make_inventory_target(2, 0), 3});
      auto first_checked = container.check_move(config, std::move(first_move));
      CASE_EXPECT_EQ(first_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      CASE_EXPECT_EQ(container.move(first_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

      entry = container.get(gpos00);
      ItemMoveRequest second_move;
      second_move.move_sub_entrys.push_back({entry, 2});
      second_move.move_add_entrys.push_back({entry, make_inventory_target(3, 0), 2});
      auto second_checked = container.check_move(config, std::move(second_move));
      CASE_EXPECT_EQ(second_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
      CASE_EXPECT_EQ(container.move(second_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    }
    // (0,0)=5, (1,0)=20, (2,0)=3, (3,0)=2
    CASE_EXPECT_EQ(container.get_item_count_debug(kItemTypeId_1x1), 30);
    verify_item_count_consistency(container, kItemTypeId_1x1);
    {
      auto dumped = dump_container_items(container);
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
    verify_container_dump(container);
  }

  // ----------------------------------------------------------------
  // Step 24: Container — 大物品与小物品交换位置
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 24: 大物品与小物品交换位置 ===\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& container = *grid_ptr;
    init_test_container(container);

    // 放 2x2 在 (0,0) 和 1x1 在 (4,0), (5,0)
    auto big = make_grid_item(kItemTypeId_2x2, 1, 0, 0);
    auto s1 = make_grid_item(kItemTypeId_1x1, 10, 4, 0);
    auto s2 = make_grid_item(kItemTypeId_1x1, 20, 5, 0);
    {
      ItemAddRequest reqs;
      *reqs.Add() = big;
      *reqs.Add() = s1;
      *reqs.Add() = s2;
      auto checked = container.check_add(config, make_item_readable_iterable(reqs));
      container.add(checked);
    }

    // 交换: 2x2(0,0) → (4,0), 两个 1x1 → (0,0) 和 (1,0)
    PROJECT_NAMESPACE_ID::DItemBasic big_sub = make_sub_basic(kItemTypeId_2x2, 1, 0, 0);
    PROJECT_NAMESPACE_ID::DItemBasic s1_sub = make_sub_basic(kItemTypeId_1x1, 10, 4, 0);
    PROJECT_NAMESPACE_ID::DItemBasic s2_sub = make_sub_basic(kItemTypeId_1x1, 20, 5, 0);
    (void)big_sub;
    (void)s1_sub;
    (void)s2_sub;
    PROJECT_NAMESPACE_ID::DItemGridPosition big_pos, s1_pos, s2_pos;
    big_pos.mutable_user_inventory()->set_x(0);
    big_pos.mutable_user_inventory()->set_y(0);
    s1_pos.mutable_user_inventory()->set_x(4);
    s1_pos.mutable_user_inventory()->set_y(0);
    s2_pos.mutable_user_inventory()->set_x(5);
    s2_pos.mutable_user_inventory()->set_y(0);
    auto big_entry = container.get(big_pos);
    auto s1_entry = container.get(s1_pos);
    auto s2_entry = container.get(s2_pos);
    ItemMoveRequest move_req;
    move_req.move_sub_entrys.push_back({big_entry, 1});
    move_req.move_sub_entrys.push_back({s1_entry, 10});
    move_req.move_sub_entrys.push_back({s2_entry, 20});
    move_req.move_add_entrys.push_back({big_entry, make_inventory_target(4, 0), 1});
    move_req.move_add_entrys.push_back({s1_entry, make_inventory_target(0, 0), 10});
    move_req.move_add_entrys.push_back({s2_entry, make_inventory_target(1, 0), 20});

    auto checked = container.check_move(config, std::move(move_req));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    auto result = container.move(checked);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 验证
    {
      auto dumped = dump_container_items(container);
      auto* big_at40 = find_dumped_by_position(dumped, kItemTypeId_2x2, 4, 0);
      auto* s1_at00 = find_dumped_by_position(dumped, kItemTypeId_1x1, 0, 0);
      auto* s2_at10 = find_dumped_by_position(dumped, kItemTypeId_1x1, 1, 0);
      CASE_EXPECT_TRUE(big_at40 != nullptr);
      CASE_EXPECT_TRUE(s1_at00 != nullptr);
      CASE_EXPECT_TRUE(s2_at10 != nullptr);
    }
    // 占格标记验证
    {
      const auto& flags = container.get_occupy_grid_flag();
      CASE_EXPECT_TRUE(flags.is_occupied(0, 0));   // 1x1 at (0,0)
      CASE_EXPECT_TRUE(flags.is_occupied(1, 0));   // 1x1 at (1,0)
      CASE_EXPECT_FALSE(flags.is_occupied(0, 1));  // 原来 2x2 占 (0,0)~(1,1), 现在释放
      CASE_EXPECT_TRUE(flags.is_occupied(4, 0));   // 2x2 at (4,0)
      CASE_EXPECT_TRUE(flags.is_occupied(5, 0));   // 2x2 at (5,0)→col=5
      CASE_EXPECT_TRUE(flags.is_occupied(4, 1));   // 2x2 row=1
      CASE_EXPECT_TRUE(flags.is_occupied(5, 1));
    }
    verify_container_dump(container);
  }

  // ----------------------------------------------------------------
  // Step 25: Container — 显式跨 Grid Move (sub + add)
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 25: 跨 Grid Move ===\n";
  {
    auto inventory_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& inventory = *inventory_ptr;
    init_test_container(inventory);
    auto backpack_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& backpack = *backpack_ptr;
    backpack.init(10, 10, PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterInventory, 0);
    backpack.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
    backpack.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
    register_test_log_handler(backpack);

    // inventory 添加 1x1 (2,3) count=10
    {
      auto item = make_grid_item(kItemTypeId_1x1, 10, 2, 3);
      ItemAddRequest reqs;
      *reqs.Add() = item;
      auto checked = inventory.check_add(config, make_item_readable_iterable(reqs));
      inventory.add(checked);
    }
    CASE_EXPECT_EQ(inventory.get_item_count_debug(kItemTypeId_1x1), 10);
    verify_item_count_consistency(inventory, kItemTypeId_1x1);

    // 跨 Grid: inventory (2,3) → backpack (0,0), 6 个
    ItemSubRequest sub_requests;
    *sub_requests.Add() = make_sub_basic(kItemTypeId_1x1, 6, 2, 3);
    auto checked_sub = inventory.check_sub(config, make_item_readable_iterable(sub_requests));
    CASE_EXPECT_EQ(checked_sub.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(inventory.sub(checked_sub).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    auto target_item = make_grid_item(kItemTypeId_1x1, 6, 0, 0);
    target_item.mutable_item_basic()->mutable_position()->mutable_grid_position()->mutable_character_inventory();
    ItemAddRequest add_requests;
    *add_requests.Add() = target_item;
    auto checked_add = backpack.check_add(config, make_item_readable_iterable(add_requests));
    CASE_EXPECT_EQ(checked_add.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(backpack.add(checked_add).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 验证
    CASE_EXPECT_EQ(inventory.get_item_count_debug(kItemTypeId_1x1), 4);
    verify_item_count_consistency(inventory, kItemTypeId_1x1);
    CASE_EXPECT_EQ(backpack.get_item_count_debug(kItemTypeId_1x1), 6);
    verify_item_count_consistency(backpack, kItemTypeId_1x1);
    // find_entry_by_id 校验: 源 entry 仍在(数量减少), 目标为新 entry, 各 Grid 索引相互独立
    {
      uint64_t src_eid = (*inventory.get_group(kItemTypeId_1x1)->begin())->entry_id();
      uint64_t dst_eid = (*backpack.get_group(kItemTypeId_1x1)->begin())->entry_id();
      verify_find_entry_by_id(inventory, src_eid, kItemTypeId_1x1, 4, 0);
      verify_find_entry_by_id(backpack, dst_eid, kItemTypeId_1x1, 6, 0);
    }
    {
      auto inv_dumped = dump_container_items(inventory);
      auto* at23 = find_dumped_by_position(inv_dumped, kItemTypeId_1x1, 2, 3);
      CASE_EXPECT_TRUE(at23 != nullptr);
      if (at23) CASE_EXPECT_EQ(at23->item_basic().count(), static_cast<int64_t>(4));
    }
    {
      auto bp_dumped = dump_container_items(backpack);
      auto* at00 = find_dumped_by_backpack_position(bp_dumped, kItemTypeId_1x1, 0, 0);
      CASE_EXPECT_TRUE(at00 != nullptr);
      if (at00) CASE_EXPECT_EQ(at00->item_basic().count(), static_cast<int64_t>(6));
    }
    // 两个独立容器的 entry_id 都从 1 开始
    PROJECT_NAMESPACE_ID::DItemGridPosition bp00;
    bp00.mutable_character_inventory()->set_x(0);
    bp00.mutable_character_inventory()->set_y(0);
    auto bp_entry = backpack.get(bp00);
    CASE_EXPECT_TRUE(bp_entry != nullptr);
    if (bp_entry) CASE_EXPECT_EQ(bp_entry->entry_id(), static_cast<uint64_t>(1));

    verify_container_dump(inventory);
    verify_container_dump(backpack);
  }

  // ----------------------------------------------------------------
  // Step 26: 继续主线 — 回到主 server/client, 再获得一波占格奖励后验证整体状态
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Step 26: 继续主线 — 最终奖励 ===\n";
  {
    auto new_item = make_grid_item(kItemTypeId_1x1, 50, 1, 0);  // (1,0) 已空
    auto new_equip = make_equip_item(1004, 6, 0);
    ItemAddRequest reqs;
    *reqs.Add() = new_item;
    *reqs.Add() = new_equip;
    auto checked = server.check_add(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    server.add(checked);
  }
  CASE_EXPECT_EQ(server.get_item_count_debug(kItemTypeId_1x1), 140);  // 50(2,0) + 40(5,0) + 50(1,0)
  verify_item_count_consistency(server, kItemTypeId_1x1);
  CASE_EXPECT_EQ(server.get_item_count_debug(kEquipmentTypeId), 3);  // 1001, 1003, 1004
  verify_item_count_consistency(server, kEquipmentTypeId);
  CASE_EXPECT_TRUE(server.get_by_guid(1004) != nullptr);
  verify_container_dump(server);
  sync_and_verify(server, client, config, "Step 26: 最终奖励");

  // ----------------------------------------------------------------
  // 最终状态汇总验证
  // ----------------------------------------------------------------
  CASE_MSG_INFO() << "=== Final: 最终状态汇总验证 ===\n";
  {
    // 遍历 server 和 client, 确认条目数一致
    auto server_dumped = dump_container_items(server);
    auto client_dumped = dump_container_items(client);
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

    verify_container_dump(server);
    verify_container_dump(client);
  }

  CASE_MSG_INFO() << "=== 用户游玩模拟完成 ===\n";
}

// ============================================================
// replace 单元测试
// ============================================================

CASE_TEST(ItemContainer, replace_with_client_sync) {
  auto server_ptr = atfw::util::memory::make_strong_rc<ServerTestItemFiniteGridContainer>();
  auto& server = *server_ptr;
  init_server_container(server);
  server.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto client_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& client = *client_ptr;
  init_test_container(client);
  client.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

  auto config = make_test_config_group();

  // ---- 准备: 通过 add 放入旧数据 (占格x2/装备GUID) ----
  auto item_a = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
  auto item_b = make_grid_item(kItemTypeId_1x1, 20, 1, 0);
  auto equip = make_equip_item(777, 2, 0);
  ItemAddRequest add_reqs;
  *add_reqs.Add() = item_a;
  *add_reqs.Add() = item_b;
  *add_reqs.Add() = equip;
  auto result = server.check_add(config, make_item_readable_iterable(add_reqs));
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
  ItemReplaceRequest rep_reqs;
  *rep_reqs.Add() = new_item;
  *rep_reqs.Add() = new_equip;
  ItemReplaceRequest restore_reqs = rep_reqs;
  auto rep_checked = server.check_replace(config, make_item_readable_iterable(rep_reqs));
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
  ItemReplaceRequest empty_reqs;
  auto empty_checked = server.check_replace(config, make_item_readable_iterable(empty_reqs));
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
  auto checkede = server.check_replace(config, make_item_readable_iterable(restore_reqs));
  server.replace(checkede);
  CASE_EXPECT_TRUE(server.get_by_guid(888) != nullptr);

  // 1. 空实例
  {
    PROJECT_NAMESPACE_ID::DItemInstance invalid_item;
    ItemReplaceRequest reqs;
    *reqs.Add() = new_item;
    *reqs.Add() = invalid_item;
    auto checked = server.check_replace(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  // 2. 重复 GUID
  {
    auto e1 = make_equip_item(999, 0, 0);
    auto e2 = make_equip_item(999, 1, 0);
    ItemReplaceRequest reqs;
    *reqs.Add() = e1;
    *reqs.Add() = e2;
    auto checked = server.check_replace(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID);
  }
  // 3. 同位置不同类型 → 占用
  {
    auto i1 = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    auto i2 = make_grid_item(kItemTypeId_2x2, 1, 0, 0);
    ItemReplaceRequest reqs;
    *reqs.Add() = i1;
    *reqs.Add() = i2;
    auto checked = server.check_replace(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED);
  }
  // 4. 数量超过堆叠上限
  {
    auto i1 = make_grid_item(kItemTypeId_1x1, 100, 0, 0);
    ItemReplaceRequest reqs;
    *reqs.Add() = i1;
    auto checked = server.check_replace(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW);
  }
  // 5. 越界
  {
    auto i1 = make_grid_item(kItemTypeId_2x2, 1, 9, 9);
    ItemReplaceRequest reqs;
    *reqs.Add() = i1;
    auto checked = server.check_replace(config, make_item_readable_iterable(reqs));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OUT_OF_RANGE);
  }

  // 所有失败用例后容器数据保持基准不变
  CASE_EXPECT_TRUE(server.get_by_guid(888) != nullptr);
  verify_container_dump(server);
}

// ============================================================
// find_positions_for_basics 单元测试
// ============================================================

CASE_TEST(ItemContainer, find_positions_for_basics) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)

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
  auto call_find = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
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
    return grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                          make_item_readable_iterable(ignore_field), success, failed);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(3, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemInfiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment, 0);
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
    class RejectFirstColGrid : public TestItemFiniteGridContainer {
     protected:
      int32_t on_check_add_one(
          const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
          const PROJECT_NAMESPACE_ID::DItemInstance& request) const override {
        if (request.item_basic().position().grid_position().user_inventory().x() == 0) {
          return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // 拒绝 x=0
        }
        return TestItemFiniteGridContainer::on_check_add_one(config_group, request);
      }
    };

    auto grid_ptr = atfw::util::memory::make_strong_rc<RejectFirstColGrid>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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

CASE_TEST(ItemContainer, find_positions_for_basics_ignore_item) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
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

  auto call_find = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
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
    return grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                          make_item_readable_iterable(ignore_field), success, failed);
  };

  // ============================================================
  // Case 1: 消耗 A×1 兑换 A×2, 堆叠上限 99
  //         原位 (0,0) 已有 A×1, ignore_item 消耗 1 个 → 空出容量,
  //         兑换的 2 个都堆回原位 (容量 99 足够, 无需找新位置)。
  // ============================================================
  CASE_MSG_INFO() << "Case 1: ignore_item 消耗后堆回原位\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemInfiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);

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

CASE_TEST(ItemContainer, find_positions_for_basics_review_issues) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
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

  auto call_find = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
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
    return grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                          make_item_readable_iterable(ignore_field), success, failed);
  };

  // ============================================================
  // Case 1: 策略2 批次内累计 — 已有 A×59 (limit 99), 传入 [{A,30},{A,30}]
  //         两条各规划 30 到同一位置会超限, 第二条只能吸收 10, 剩余 20 进 failed。
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 策略2 批次内累计已规划数量\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(1, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(1, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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

CASE_TEST(ItemContainer, find_positions_for_basics_two_pass) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };
  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  auto call_find = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    return grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                          make_item_readable_iterable(ignore_field), success, failed);
  };

  // ============================================================
  // Case 1: 第一次只放得下 2 件 (2x2 背包), 剩余 2 件放入 failed_item,
  //         第二次用 failed_item 作为 basics 在另一个背包放置。
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 两次搜索位置 (多背包依次塞入)\n";
  {
    // 第一个背包: 2x2, 堆叠上限 1, 只能放 2 件
    auto grid1_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid1 = *grid1_ptr;
    register_test_log_handler(grid1);
    grid1.init(2, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    grid1.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    // 第二个背包: 2x2, 堆叠上限 1, 也能放 2 件
    auto grid2_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid2 = *grid2_ptr;
    register_test_log_handler(grid2);
    grid2.init(2, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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

CASE_TEST(ItemContainer, find_positions_for_instances) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
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

  auto call_find = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemInstance>& items,
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
    return grid.find_positions_for_instances(config, make_item_readable_iterable(items_field),
                                             make_item_readable_iterable(ignore_field), success, failed);
  };

  // ============================================================
  // Case 1: 占格道具找位置, 输出 DItemInstance (带位置)
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 占格道具找位置\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);

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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(1, 1, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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

CASE_TEST(ItemContainer, find_positions_fill_container_guid) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();
  constexpr int64_t kContainerGuid = 10001;

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };

  auto call_find_basics = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                              google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                              google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    return grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                          make_item_readable_iterable(ignore_field), success, failed);
  };

  // ============================================================
  // Case 1: 无位置模式 (kNoPosition) — 输出填充 container_guid
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 无位置模式输出填充 container_guid\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, kContainerGuid);

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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, kContainerGuid);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, kContainerGuid);
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
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, kContainerGuid);
    grid.register_position_cfg(kItemTypeId_1x1, 1, 1, 1);

    PROJECT_NAMESPACE_ID::DItemInstance inst;
    inst.mutable_item_basic()->set_type_id(kItemTypeId_1x1);
    inst.mutable_item_basic()->set_count(1);
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> items_field;
    *items_field.Add() = inst;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> success;
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> failed;
    bool ok = grid.find_positions_for_instances(config, make_item_readable_iterable(items_field),
                                                make_item_readable_iterable(ignore_field), success, failed);
    CASE_EXPECT_TRUE(ok);
    CASE_EXPECT_EQ(1u, success.size());
    CASE_EXPECT_EQ(kContainerGuid, success[0].item_basic().position().container_guid());
  }
}

// ============================================================
// find_positions — 背包仅剩不同大小位置时, 从大到小正好放入
// ============================================================

CASE_TEST(ItemContainer, find_positions_fit_various_sizes) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = ::excel::excel_config_type_traits::make_shared<::excel::config_group_t>();

  auto make_basic = [](int32_t type_id, int64_t count) {
    PROJECT_NAMESPACE_ID::DItemBasic b;
    b.set_type_id(type_id);
    b.set_count(count);
    return b;
  };

  auto call_find = [&](auto& grid, const std::vector<PROJECT_NAMESPACE_ID::DItemBasic>& basics,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success,
                       google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed) {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics_field;
    for (const auto& b : basics) {
      *basics_field.Add() = b;
    }
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_field;
    return grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                          make_item_readable_iterable(ignore_field), success, failed);
  };

  auto get_x = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().x(); };
  auto get_y = [](const PROJECT_NAMESPACE_ID::DItemGridPosition& p) { return p.user_inventory().y(); };

  // ============================================================
  // Case 1: 4x4 背包仅剩 2x2 / 2x1 / 1x1 三个区域, 从大到小正好放入
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 不同大小位置从大到小正好放入\n";
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    register_test_log_handler(grid);
    grid.init(4, 4, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
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

CASE_TEST(ItemContainer, checked_request_validation) {
  constexpr int64_t kPrimaryContainerGuid = 10001;
  constexpr int64_t kSecondaryContainerGuid = 10002;

  auto primary_grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& primary_grid = *primary_grid_ptr;
  init_test_container(primary_grid, 10, 10, kPrimaryContainerGuid);

  auto secondary_grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& secondary_grid = *secondary_grid_ptr;
  init_test_container(secondary_grid, 10, 10, kSecondaryContainerGuid);

  auto config = make_test_config_group();

  // 使用错误 Grid 执行已检查请求必须被 container_guid 拒绝，且请求和两个 Grid 均保持未修改。
  auto first_item = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  first_item.mutable_item_basic()->mutable_position()->set_container_guid(kPrimaryContainerGuid);
  ItemAddRequest first_requests;
  *first_requests.Add() = first_item;
  auto checked_for_primary = primary_grid.check_add(config, make_item_readable_iterable(first_requests));
  CASE_EXPECT_EQ(checked_for_primary.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto wrong_grid_result = secondary_grid.add(checked_for_primary);
  CASE_EXPECT_EQ(wrong_grid_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_FALSE(checked_for_primary.apply);
  CASE_EXPECT_EQ(primary_grid.get_item_count_debug(kItemTypeId_1x1), 0);
  CASE_EXPECT_EQ(secondary_grid.get_item_count_debug(kItemTypeId_1x1), 0);

  auto primary_result = primary_grid.add(checked_for_primary);
  CASE_EXPECT_EQ(primary_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(primary_grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // 第二次 check 会使第一次结果的 operate_id 失效，只有最新结果能够执行。
  auto stale_item = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
  stale_item.mutable_item_basic()->mutable_position()->set_container_guid(kPrimaryContainerGuid);
  ItemAddRequest stale_requests;
  *stale_requests.Add() = stale_item;
  auto stale_checked = primary_grid.check_add(config, make_item_readable_iterable(stale_requests));
  CASE_EXPECT_EQ(stale_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto latest_item = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  latest_item.mutable_item_basic()->mutable_position()->set_container_guid(kPrimaryContainerGuid);
  ItemAddRequest latest_requests;
  *latest_requests.Add() = latest_item;
  auto latest_checked = primary_grid.check_add(config, make_item_readable_iterable(latest_requests));
  CASE_EXPECT_EQ(latest_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  auto stale_result = primary_grid.add(stale_checked);
  CASE_EXPECT_EQ(stale_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_FALSE(stale_checked.apply);
  CASE_EXPECT_EQ(primary_grid.get_item_count_debug(kItemTypeId_1x1), 1);

  auto latest_result = primary_grid.add(latest_checked);
  CASE_EXPECT_EQ(latest_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(primary_grid.get_item_count_debug(kItemTypeId_1x1), 2);
  verify_container_dump(primary_grid);
}

CASE_TEST(ItemContainer, no_position_ungrid_lifecycle) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  auto& grid = *grid_ptr;
  grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  *add_requests.Add() = make_ungrid_item(kCoinTypeId, 10);
  auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 10);
  verify_item_count_consistency(grid, kCoinTypeId);

  ItemSubRequest sub_requests;
  *sub_requests.Add() = make_sub_basic(kCoinTypeId, 4);
  auto sub_checked = grid.check_sub(config, make_item_readable_iterable(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.sub(sub_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 6);
  verify_item_count_consistency(grid, kCoinTypeId);

  // check_replace 必须在 clone 后仍保留无位置模式。
  ItemReplaceRequest replace_requests;
  *replace_requests.Add() = make_ungrid_item(kVirtualTypeId, 8);
  auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.replace(replace_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 0);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kVirtualTypeId), 8);
  verify_item_count_consistency(grid, kVirtualTypeId);

  CASE_EXPECT_TRUE(grid.load(config, make_ungrid_item(kVirtualTypeId, 3)));
  CASE_EXPECT_EQ(grid.get_item_count_debug(kVirtualTypeId), 11);
  verify_item_count_consistency(grid, kVirtualTypeId);

  const auto* virtual_group = grid.get_group(kVirtualTypeId);
  CASE_EXPECT_TRUE(virtual_group != nullptr && !virtual_group->empty());
  if (!virtual_group || virtual_group->empty()) {
    return;
  }
  uint64_t virtual_entry_id = (*virtual_group->begin())->entry_id();

  // 客户端增量同步也必须在无位置模式中维护普通道具的 entry_id 与数量。
  call_apply_entries(grid, config, {}, {{virtual_entry_id, make_ungrid_item(kVirtualTypeId, 12)}});
  CASE_EXPECT_EQ(grid.get_item_count_debug(kVirtualTypeId), 12);
  verify_item_count_consistency(grid, kVirtualTypeId);
  verify_find_entry_by_id(grid, virtual_entry_id, kVirtualTypeId, 12, 0);

  call_apply_entries(grid, config, {virtual_entry_id}, {});
  CASE_EXPECT_EQ(grid.get_item_count_debug(kVirtualTypeId), 0);
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
  CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                                  make_item_readable_iterable(ignore_field), success, failed));
  CASE_EXPECT_EQ(success.size(), static_cast<size_t>(2));
  CASE_EXPECT_TRUE(failed.empty());
  if (success.size() == 2) {
    CASE_EXPECT_FALSE(success[0].position().grid_position().has_virtual_inventory());
    CASE_EXPECT_FALSE(success[1].position().grid_position().has_virtual_inventory());
  }
}

CASE_TEST(ItemContainer, no_position_rejects_positional_items) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  auto& grid = *grid_ptr;
  grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  ItemAddRequest setup_requests;
  *setup_requests.Add() = make_ungrid_item(kCoinTypeId, 5);
  auto setup_checked = grid.check_add(config, make_item_readable_iterable(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  ItemAddRequest grid_item_requests;
  *grid_item_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 3, 4);
  auto grid_item_checked = grid.check_add(config, make_item_readable_iterable(grid_item_requests));
  CASE_EXPECT_EQ(grid_item_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.add(grid_item_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 5);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 0);

  ItemAddRequest equipment_requests;
  *equipment_requests.Add() = make_equip_item(10001, 1, 2);
  auto equipment_checked = grid.check_add(config, make_item_readable_iterable(equipment_requests));
  CASE_EXPECT_EQ(equipment_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.add(equipment_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);

  // replace 经由空 clone 复用 check_add，不能在 clone 中重新允许占格物品。
  ItemReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.replace(replace_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 5);

  CASE_EXPECT_FALSE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 0, 0)));
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 5);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 0);
}

CASE_TEST(ItemContainer, check_item_position_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  // 准备一个合法条目，供 sub、move 和 replace 状态不变断言使用。
  ItemAddRequest setup_requests;
  *setup_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto setup_checked = grid.check_add(config, make_item_readable_iterable(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  // check_add
  state.rejected_position_x = 1;
  state.reset_call_counts();
  ItemAddRequest add_requests;
  *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
  auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // check_sub
  state.rejected_position_x = 0;
  state.reset_call_counts();
  ItemSubRequest sub_requests;
  *sub_requests.Add() = make_sub_basic(kItemTypeId_1x1, 1, 0, 0);
  auto sub_checked = grid.check_sub(config, make_item_readable_iterable(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

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
  ItemMoveRequest source_move;
  source_move.move_sub_entrys.push_back({source_entry, 1});
  source_move.move_add_entrys.push_back({source_entry, make_inventory_target(1, 0), 1});
  auto source_move_checked = grid.check_move(config, std::move(source_move));
  CASE_EXPECT_EQ(source_move_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // check_move 的目标位置
  state.rejected_position_x = 1;
  state.reset_call_counts();
  ItemMoveRequest target_move;
  target_move.move_sub_entrys.push_back({source_entry, 1});
  target_move.move_add_entrys.push_back({source_entry, make_inventory_target(1, 0), 1});
  auto target_move_checked = grid.check_move(config, std::move(target_move));
  CASE_EXPECT_EQ(target_move_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 2);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // check_replace 经由 clone 的 check_add 也必须保留位置钩子。
  state.rejected_position_x = 2;
  state.reset_call_counts();
  ItemReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // load
  state.rejected_position_x = 3;
  state.reset_call_counts();
  CASE_EXPECT_FALSE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 3, 0)));
  CASE_EXPECT_EQ(state.check_item_position_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);
}

CASE_TEST(ItemContainer, on_check_add_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  // check_add
  state.reject_all_add = true;
  ItemAddRequest add_requests;
  *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_add_calls, 1);
  CASE_EXPECT_TRUE(grid.is_empty());

  // check_replace 经由 clone 调用 on_check_add。
  state.reset_call_counts();
  ItemReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
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
  CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, make_item_readable_iterable(basics_field),
                                                  make_item_readable_iterable(ignore_field), success, failed));
  CASE_EXPECT_EQ(success.size(), static_cast<size_t>(1));
  CASE_EXPECT_EQ(success[0].position().grid_position().user_inventory().x(), 1);
  CASE_EXPECT_TRUE(state.on_check_add_calls >= 2);
}

CASE_TEST(ItemContainer, on_check_sub_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  ItemAddRequest setup_requests;
  *setup_requests.Add() = make_grid_item(kItemTypeId_1x1, 2, 0, 0);
  auto setup_checked = grid.check_add(config, make_item_readable_iterable(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  state.reject_sub = true;
  state.reset_call_counts();
  ItemSubRequest sub_requests;
  *sub_requests.Add() = make_sub_basic(kItemTypeId_1x1, 1, 0, 0);
  auto sub_checked = grid.check_sub(config, make_item_readable_iterable(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_sub_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 2);
}

CASE_TEST(ItemContainer, on_check_item_count_limit_hook) {
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid);
  auto config = make_test_config_group();
  auto& state = grid.hook_state();

  ItemAddRequest setup_requests;
  *setup_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto setup_checked = grid.check_add(config, make_item_readable_iterable(setup_requests));
  CASE_EXPECT_EQ(setup_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(setup_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  state.reject_count_limit = true;

  // check_add
  state.reset_call_counts();
  ItemAddRequest add_requests;
  *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
  auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(add_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // check_replace 经由 clone 调用数量上限钩子。
  state.reset_call_counts();
  ItemReplaceRequest replace_requests;
  *replace_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // load
  state.reset_call_counts();
  CASE_EXPECT_FALSE(grid.load(config, make_grid_item(kItemTypeId_1x1, 1, 3, 0)));
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);

  // check_move 的 add-only 路径代表外部 Grid 移入，必须执行数量上限钩子。
  auto source_grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& source_grid = *source_grid_ptr;
  init_test_container(source_grid);
  ItemAddRequest source_requests;
  *source_requests.Add() = make_grid_item(kItemTypeId_1x1, 1, 0, 0);
  auto source_checked = source_grid.check_add(config, make_item_readable_iterable(source_requests));
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
  ItemMoveRequest move_requests;
  move_requests.move_add_entrys.push_back({source_entry, make_inventory_target(1, 0), 1});
  auto move_checked = grid.check_move(config, std::move(move_requests));
  CASE_EXPECT_EQ(move_checked.result.error_code, kHookRejectedError);
  CASE_EXPECT_EQ(state.on_check_item_count_limit_calls, 1);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 1);
  CASE_EXPECT_EQ(source_grid.get_item_count_debug(kItemTypeId_1x1), 1);
}

CASE_TEST(ItemContainer, check_has) {
  auto config = make_test_config_group();

  // 占格道具按位置查询, GUID 道具按 GUID 查询。
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
    auto& grid = *grid_ptr;
    init_test_container(grid);
    grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);

    ItemAddRequest add_requests;
    *add_requests.Add() = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    *add_requests.Add() = make_equip_item(9001, 1, 0);
    auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
    CASE_EXPECT_EQ(add_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemHasRequest has_requests;
    *has_requests.Add() = make_sub_basic(kItemTypeId_1x1, 5, 0, 0);
    *has_requests.Add() = make_equip_sub_by_guid(9001);
    auto has_result = grid.check_has(config, make_item_readable_iterable(has_requests));
    CASE_EXPECT_EQ(has_result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 5);

    ItemHasRequest repeated_position_requests;
    *repeated_position_requests.Add() = make_sub_basic(kItemTypeId_1x1, 2, 0, 0);
    *repeated_position_requests.Add() = make_sub_basic(kItemTypeId_1x1, 3, 0, 0);
    CASE_EXPECT_EQ(grid.check_has(config, make_item_readable_iterable(repeated_position_requests)).error_code,
                   PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemHasRequest insufficient_requests;
    *insufficient_requests.Add() = make_sub_basic(kItemTypeId_1x1, 6, 0, 0);
    auto insufficient_result = grid.check_has(config, make_item_readable_iterable(insufficient_requests));
    CASE_EXPECT_EQ(insufficient_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);

    ItemHasRequest missing_position_requests;
    *missing_position_requests.Add() = make_sub_basic(kItemTypeId_1x1, 1, 2, 0);
    auto missing_position_result = grid.check_has(config, make_item_readable_iterable(missing_position_requests));
    CASE_EXPECT_EQ(missing_position_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND);

    ItemHasRequest missing_guid_requests;
    *missing_guid_requests.Add() = make_equip_sub_by_guid(9002);
    auto missing_guid_result = grid.check_has(config, make_item_readable_iterable(missing_guid_requests));
    CASE_EXPECT_EQ(missing_guid_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND);
  }

  // 无位置道具按类型总数查询, 同一批次的请求需要累计数量。
  {
    auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
    auto& grid = *grid_ptr;
    grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, 0);
    register_test_log_handler(grid);

    ItemAddRequest add_requests;
    *add_requests.Add() = make_ungrid_item(kCoinTypeId, 10);
    *add_requests.Add() = make_ungrid_item(kVirtualTypeId, 4);
    auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
    CASE_EXPECT_EQ(add_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.add(add_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemHasRequest has_requests;
    *has_requests.Add() = make_sub_basic(kCoinTypeId, 7);
    *has_requests.Add() = make_sub_basic(kVirtualTypeId, 4);
    CASE_EXPECT_EQ(grid.check_has(config, make_item_readable_iterable(has_requests)).error_code,
                   PROJECT_NAMESPACE_ID::EN_SUCCESS);

    ItemHasRequest repeated_type_requests;
    *repeated_type_requests.Add() = make_sub_basic(kCoinTypeId, 6);
    *repeated_type_requests.Add() = make_sub_basic(kCoinTypeId, 5);
    auto repeated_type_result = grid.check_has(config, make_item_readable_iterable(repeated_type_requests));
    CASE_EXPECT_EQ(repeated_type_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);

    ItemHasRequest missing_type_requests;
    *missing_type_requests.Add() = make_sub_basic(kVirtualTypeId, 5);
    auto missing_type_result = grid.check_has(config, make_item_readable_iterable(missing_type_requests));
    CASE_EXPECT_EQ(missing_type_result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
  }
}

// ============================================================
// 后续维护风险守卫
//
// 下面这些用例针对的是"实现改动后容易静默坏掉"的契约, 而不是某次具体需求:
// 索引与位图的清空范围、空克隆必须复制的配置、位置索引的锚点语义、
// 寻位的只读性、失败路径不得留下副作用、entry_id 索引的生命周期、
// 以及"同类型不占格道具只允许一个条目"这一让按类型扣减成立的前提。
// ============================================================

CASE_TEST(ItemContainer, clear_resets_indexes_and_occupancy) {
  constexpr int64_t container_guid = 4242;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  auto grid_item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
  set_item_container_guid(grid_item, container_guid);
  *add_requests.Add() = grid_item;
  auto item_2x2 = make_grid_item(kItemTypeId_2x2, 1, 2, 0);
  set_item_container_guid(item_2x2, container_guid);
  *add_requests.Add() = item_2x2;
  auto equip = make_equip_item(9001, 3, 3);
  set_item_container_guid(equip, container_guid);
  *add_requests.Add() = equip;

  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_2x2), 1);
  CASE_EXPECT_TRUE(grid.get_by_guid(9001) != nullptr);
  CASE_EXPECT_TRUE(grid.get_occupy_grid_flag().is_occupied(2, 0));

  grid.clear();

  CASE_EXPECT_TRUE(grid.is_empty());
  CASE_EXPECT_EQ(grid.get_entry_count(), static_cast<size_t>(0));
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 0);
  CASE_EXPECT_EQ(grid.get_item_count(kItemTypeId_1x1), 0);
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(0, 0).grid_position()) == nullptr);
  CASE_EXPECT_TRUE(grid.get_by_guid(9001) == nullptr);
  CASE_EXPECT_FALSE(grid.get_occupy_grid_flag().is_occupied(2, 0));

  // 复用原先占用的位置与 GUID 必须成功:
  // 位图没清干净会被判成"位置已占用", GUID 索引没清干净会被判成"GUID 重复"
  ItemAddRequest reuse_requests;
  auto reuse_2x2 = make_grid_item(kItemTypeId_2x2, 1, 2, 0);
  set_item_container_guid(reuse_2x2, container_guid);
  *reuse_requests.Add() = reuse_2x2;
  auto reuse_equip = make_equip_item(9001, 3, 3);
  set_item_container_guid(reuse_equip, container_guid);
  *reuse_requests.Add() = reuse_equip;

  auto reuse_checked = grid.check_add(config, make_item_readable_iterable(reuse_requests));
  CASE_EXPECT_EQ(reuse_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(reuse_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_2x2), 1);
  CASE_EXPECT_TRUE(grid.get_by_guid(9001) != nullptr);
  verify_container_dump(grid);
}

CASE_TEST(ItemContainer, replace_validates_against_container_config) {
  constexpr int64_t container_guid = 4243;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  auto seed_item = make_grid_item(kItemTypeId_1x1, 3, 0, 0);
  set_item_container_guid(seed_item, container_guid);
  *add_requests.Add() = seed_item;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  const std::vector<int32_t> tracked_types = {kItemTypeId_1x1, kItemTypeId_2x2};
  auto before = capture_container_state(grid, tracked_types);

  // 越界请求必须被拒绝, 且失败的 replace 不得改动容器
  ItemReplaceRequest out_of_range_requests;
  auto out_of_range_item = make_grid_item(kItemTypeId_1x1, 1, 9, 9);
  set_item_container_guid(out_of_range_item, container_guid);
  *out_of_range_requests.Add() = out_of_range_item;
  auto out_of_range_checked = grid.check_replace(config, make_item_readable_iterable(out_of_range_requests));
  CASE_EXPECT_EQ(out_of_range_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OUT_OF_RANGE);
  CASE_EXPECT_EQ(grid.replace(out_of_range_checked).error_code,
                 PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OUT_OF_RANGE);
  CASE_EXPECT_TRUE(container_state_equal(before, capture_container_state(grid, tracked_types)));

  // 两个不同位置的道具: 空克隆若丢了行列尺寸会被判越界, 丢了 position_type 则两个请求
  // 都会落到 (0,0) 被判位置占用, 因此这条成功断言同时守住两项配置的复制
  ItemReplaceRequest replace_requests;
  auto first_item = make_grid_item(kItemTypeId_1x1, 2, 1, 1);
  set_item_container_guid(first_item, container_guid);
  *replace_requests.Add() = first_item;
  auto second_item = make_grid_item(kItemTypeId_1x1, 4, 2, 2);
  set_item_container_guid(second_item, container_guid);
  *replace_requests.Add() = second_item;

  auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
  CASE_EXPECT_EQ(replace_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.replace(replace_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 6);
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(1, 1).grid_position()) != nullptr);
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(2, 2).grid_position()) != nullptr);
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(0, 0).grid_position()) == nullptr);
  verify_container_dump(grid);
}

CASE_TEST(ItemContainer, area_occupancy_is_rectangular_and_lookup_is_anchor_only) {
  constexpr int64_t container_guid = 4244;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  auto item_2x2 = make_grid_item(kItemTypeId_2x2, 1, 0, 0);
  set_item_container_guid(item_2x2, container_guid);
  *add_requests.Add() = item_2x2;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  // 位图必须恰好覆盖矩形区域 (x 为列, y 为行), 多标或少标都会放错或挡错道具
  const auto& flags = grid.get_occupy_grid_flag();
  CASE_EXPECT_EQ(flags.row_count(), static_cast<size_t>(4));
  CASE_EXPECT_EQ(flags.column_count(), static_cast<size_t>(4));
  for (int32_t y = 0; y < 4; ++y) {
    for (int32_t x = 0; x < 4; ++x) {
      bool expected_occupied = (x < 2 && y < 2);
      CASE_EXPECT_EQ(flags.is_occupied(x, y), expected_occupied);
    }
  }

  // 位置索引按锚点记录: 锚点取得到, 被覆盖但不是锚点的格子取不到
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(0, 0).grid_position()) != nullptr);
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(1, 1).grid_position()) == nullptr);

  // 落在已覆盖格子上的新锚点必须被拒绝 (碰撞按矩形判断, 不能只看锚点)
  ItemAddRequest overlap_requests;
  auto overlap_item = make_grid_item(kItemTypeId_1x1, 1, 1, 1);
  set_item_container_guid(overlap_item, container_guid);
  *overlap_requests.Add() = overlap_item;
  auto overlap_checked = grid.check_add(config, make_item_readable_iterable(overlap_requests));
  CASE_EXPECT_EQ(overlap_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED);
  CASE_EXPECT_EQ(grid.add(overlap_checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED);

  // 空白区域仍可放入
  ItemAddRequest free_requests;
  auto free_item = make_grid_item(kItemTypeId_1x1, 2, 3, 3);
  set_item_container_guid(free_item, container_guid);
  *free_requests.Add() = free_item;
  auto free_checked = grid.check_add(config, make_item_readable_iterable(free_requests));
  CASE_EXPECT_EQ(free_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(free_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 2);
  verify_container_dump(grid);
}

CASE_TEST(ItemContainer, find_positions_is_read_only_plan) {
  constexpr int64_t container_guid = 4245;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  auto seed_item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
  set_item_container_guid(seed_item, container_guid);
  *add_requests.Add() = seed_item;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  const std::vector<int32_t> tracked_types = {kItemTypeId_1x1, kItemTypeId_2x2};
  auto before = capture_container_state(grid, tracked_types);

  // 消耗 (0,0) 的 5 件, 同时为 1x1×3 与 2x2×1 找位置
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics;
  *basics.Add() = make_sub_basic(kItemTypeId_1x1, 3, 1, 0);
  *basics.Add() = make_sub_basic(kItemTypeId_2x2, 1, 2, 2);

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_items;
  auto ignore_basic = make_sub_basic(kItemTypeId_1x1, 5, 0, 0);
  set_basic_container_guid(ignore_basic, container_guid);
  *ignore_items.Add() = ignore_basic;

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success_items;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed_items;

  CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, make_item_readable_iterable(basics),
                                                  make_item_readable_iterable(ignore_items), success_items,
                                                  failed_items));
  CASE_EXPECT_EQ(success_items.size(), 2);
  CASE_EXPECT_TRUE(failed_items.empty());

  // 寻位只是规划: 返回结果后容器状态必须完全不变, 否则 check 阶段就产生了副作用
  CASE_EXPECT_TRUE(container_state_equal(before, capture_container_state(grid, tracked_types)));
  verify_container_dump(grid);
}

CASE_TEST(ItemContainer, load_failure_has_no_side_effect) {
  constexpr int64_t container_guid = 4246;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);
  auto config = make_test_config_group();

  // 基线: 1x1×4 于 (0,0), 2x2×1 于 (2,0), 装备 500 于 (3,3)
  ItemAddRequest add_requests;
  auto seed_item = make_grid_item(kItemTypeId_1x1, 4, 0, 0);
  set_item_container_guid(seed_item, container_guid);
  *add_requests.Add() = seed_item;
  auto seed_2x2 = make_grid_item(kItemTypeId_2x2, 1, 2, 0);
  set_item_container_guid(seed_2x2, container_guid);
  *add_requests.Add() = seed_2x2;
  auto seed_equip = make_equip_item(500, 3, 3);
  set_item_container_guid(seed_equip, container_guid);
  *add_requests.Add() = seed_equip;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  const std::vector<int32_t> tracked_types = {kItemTypeId_1x1, kItemTypeId_2x2, kEquipmentTypeId};
  auto before = capture_container_state(grid, tracked_types);

  // 越界
  auto out_of_range_item = make_grid_item(kItemTypeId_1x1, 1, 9, 9);
  set_item_container_guid(out_of_range_item, container_guid);
  CASE_EXPECT_FALSE(grid.load(config, out_of_range_item));

  // 锚点已被其它类型占用 (2x2 的锚点在 (2,0))
  auto occupied_item = make_grid_item(kItemTypeId_1x1, 1, 2, 0);
  set_item_container_guid(occupied_item, container_guid);
  CASE_EXPECT_FALSE(grid.load(config, occupied_item));

  // GUID 重复
  auto duplicate_guid_item = make_equip_item(500, 3, 3);
  set_item_container_guid(duplicate_guid_item, container_guid);
  CASE_EXPECT_FALSE(grid.load(config, duplicate_guid_item));

  // 容器归属不匹配 (实例容器 GUID 保持 0)
  auto foreign_item = make_grid_item(kItemTypeId_1x1, 1, 1, 1);
  CASE_EXPECT_FALSE(grid.load(config, foreign_item));

  // 未知道具类型
  auto unknown_item = make_grid_item(999999, 1, 1, 1);
  set_item_container_guid(unknown_item, container_guid);
  CASE_EXPECT_FALSE(grid.load(config, unknown_item));

  // 全部失败路径都不得留下任何副作用
  CASE_EXPECT_TRUE(container_state_equal(before, capture_container_state(grid, tracked_types)));
  verify_container_dump(grid);
}

CASE_TEST(ItemContainer, removed_entry_stays_findable_until_last_owner_releases) {
  constexpr int64_t container_guid = 4247;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  auto seed_item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
  set_item_container_guid(seed_item, container_guid);
  *add_requests.Add() = seed_item;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  item_entry_ptr_t entry = grid.get(make_inventory_target(0, 0).grid_position());
  CASE_EXPECT_TRUE(entry != nullptr);
  if (!entry) {
    return;
  }
  uint64_t entry_id = entry->entry_id();

  // 按位置扣光 → 条目离开容器
  ItemSubRequest sub_requests;
  auto sub_basic = make_sub_basic(kItemTypeId_1x1, 5, 0, 0);
  set_basic_container_guid(sub_basic, container_guid);
  *sub_requests.Add() = sub_basic;
  auto sub_checked = grid.check_sub(config, make_item_readable_iterable(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.sub(sub_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 0);
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(0, 0).grid_position()) == nullptr);

  // 外部还持有该条目: 同步逻辑按 entry_id 取到 count=0 的条目并据此判定"已删除",
  // 这条约定是客户端增量同步正确性的前提
  item_entry_ptr_t still_found = grid.find_entry_by_id(entry_id);
  CASE_EXPECT_TRUE(still_found != nullptr);
  if (still_found) {
    CASE_EXPECT_EQ(still_found->item_instance().item_basic().count(), 0);
  }

  // 释放最后一个持有者后必须解析不到, 否则 entry_id 索引会泄漏
  entry = nullptr;
  still_found = nullptr;
  CASE_EXPECT_TRUE(grid.find_entry_by_id(entry_id) == nullptr);
  CASE_EXPECT_EQ(grid.get_entry_count(), static_cast<size_t>(0));
  CASE_EXPECT_TRUE(grid.is_empty());
}

CASE_TEST(ItemContainer, add_merges_ungrid_items_into_single_entry) {
  constexpr int64_t container_guid = 6161;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  auto& grid = *grid_ptr;
  grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, container_guid);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  ItemAddRequest first_requests;
  auto first_item = make_ungrid_item(kCoinTypeId, 10);
  set_item_container_guid(first_item, container_guid);
  *first_requests.Add() = first_item;
  auto first_checked = grid.check_add(config, make_item_readable_iterable(first_requests));
  CASE_EXPECT_EQ(first_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(first_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  ItemAddRequest second_requests;
  auto second_item = make_ungrid_item(kCoinTypeId, 5);
  set_item_container_guid(second_item, container_guid);
  *second_requests.Add() = second_item;
  auto second_checked = grid.check_add(config, make_item_readable_iterable(second_requests));
  CASE_EXPECT_EQ(second_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(second_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  // 同类型不占格道具必须只剩一个条目: 按类型扣减只会从第一个条目扣,
  // 一旦出现第二个条目, check_sub 按总数放行而 sub 只扣一个, 会少扣道具
  const auto* group = grid.get_group(kCoinTypeId);
  CASE_EXPECT_TRUE(group != nullptr);
  if (group) {
    CASE_EXPECT_EQ(group->size(), static_cast<size_t>(1));
    CASE_EXPECT_EQ(group->begin()->get()->item_instance().item_basic().count(), 15);
  }
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 15);
  verify_item_count_consistency(grid, kCoinTypeId);

  ItemSubRequest sub_requests;
  auto sub_basic = make_sub_basic(kCoinTypeId, 12);
  set_basic_container_guid(sub_basic, container_guid);
  *sub_requests.Add() = sub_basic;
  auto sub_checked = grid.check_sub(config, make_item_readable_iterable(sub_requests));
  CASE_EXPECT_EQ(sub_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.sub(sub_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 3);
  verify_item_count_consistency(grid, kCoinTypeId);
  const auto* remaining_group = grid.get_group(kCoinTypeId);
  CASE_EXPECT_TRUE(remaining_group != nullptr && !remaining_group->empty());
  if (remaining_group && !remaining_group->empty()) {
    CASE_EXPECT_EQ((*remaining_group->begin())->item_instance().item_basic().count(), static_cast<int64_t>(3));
  }
}

// ============================================================
// 同步接口与寻位的边界契约
// ============================================================

CASE_TEST(ItemContainer, apply_entries_second_ungrid_item_is_counted) {
  constexpr int64_t container_guid = 7301;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  auto& grid = *grid_ptr;
  grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, container_guid);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  ItemAddRequest first_requests;
  auto first_item = make_ungrid_item(kCoinTypeId, 10);
  set_item_container_guid(first_item, container_guid);
  *first_requests.Add() = first_item;
  auto first_checked = grid.check_add(config, make_item_readable_iterable(first_requests));
  CASE_EXPECT_EQ(first_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(first_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  // 客户端同步带来同类型的第二个条目 (entry_id 不同): 两条都必须计入
  auto extra_item = make_ungrid_item(kCoinTypeId, 4);
  set_item_container_guid(extra_item, container_guid);
  call_apply_entries(grid, config, {}, {{7301, extra_item}});

  CASE_EXPECT_EQ(grid.get_item_count_debug(kCoinTypeId), 14);
  verify_item_count_consistency(grid, kCoinTypeId);
  CASE_EXPECT_TRUE(grid.find_entry_by_id(7301) != nullptr);
}

CASE_TEST(ItemContainer, apply_entries_rejects_invalid_payload) {
  constexpr int64_t container_guid = 7302;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  auto config = make_test_config_group();

  ItemAddRequest add_requests;
  auto seed_item = make_grid_item(kItemTypeId_1x1, 3, 0, 0);
  set_item_container_guid(seed_item, container_guid);
  *add_requests.Add() = seed_item;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  item_entry_ptr_t entry = grid.get(make_inventory_target(0, 0).grid_position());
  CASE_EXPECT_TRUE(entry != nullptr);
  if (!entry) {
    return;
  }
  uint64_t entry_id = entry->entry_id();

  // 合法同步包: 数量 3 → 7 必须生效 (作为下面"非法包被拒"的对照)
  auto valid_item = make_grid_item(kItemTypeId_1x1, 7, 0, 0);
  set_item_container_guid(valid_item, container_guid);
  call_apply_entries(grid, config, {}, {{entry_id, valid_item}});
  CASE_EXPECT_EQ(grid.get_item_count_debug(kItemTypeId_1x1), 7);

  const std::vector<int32_t> tracked_types = {kItemTypeId_1x1};
  auto before = capture_container_state(grid, tracked_types);

  // 非法包 1: 数量为 0
  auto zero_count_item = make_grid_item(kItemTypeId_1x1, 0, 0, 0);
  set_item_container_guid(zero_count_item, container_guid);
  call_apply_entries(grid, config, {}, {{entry_id, zero_count_item}});
  CASE_EXPECT_TRUE(container_state_equal(before, capture_container_state(grid, tracked_types)));

  // 非法包 2: 容器归属是别的容器
  auto foreign_item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
  foreign_item.mutable_item_basic()->mutable_position()->set_container_guid(container_guid + 1);
  call_apply_entries(grid, config, {}, {{entry_id, foreign_item}});
  CASE_EXPECT_TRUE(container_state_equal(before, capture_container_state(grid, tracked_types)));

  // 非法包 3: 未知道具类型
  auto unknown_item = make_grid_item(999999, 5, 0, 0);
  set_item_container_guid(unknown_item, container_guid);
  call_apply_entries(grid, config, {}, {{entry_id, unknown_item}});
  CASE_EXPECT_TRUE(container_state_equal(before, capture_container_state(grid, tracked_types)));

  verify_container_dump(grid);
}

CASE_TEST(ItemContainer, find_positions_for_infinite_fills_container_guid) {
  constexpr int64_t container_guid = 7303;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemInfiniteEquipmentSlotContainer>();
  auto& grid = *grid_ptr;
  grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kCharacterEquipment, container_guid);
  grid.register_position_cfg(kEquipmentTypeId, 1, 1, 1);
  register_test_log_handler(grid);
  auto config = make_test_config_group();

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> basics;
  *basics.Add() = make_sub_basic(kEquipmentTypeId, 1, 0, 0, 501);

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> ignore_items;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> success_items;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic> failed_items;

  CASE_EXPECT_TRUE(grid.find_positions_for_basics(config, make_item_readable_iterable(basics),
                                                  make_item_readable_iterable(ignore_items), success_items,
                                                  failed_items));
  CASE_EXPECT_EQ(success_items.size(), 1);
  if (success_items.size() != 1) {
    return;
  }

  // 与其它模式一致地写上容器归属, 否则结果无法直接喂给 check_add
  CASE_EXPECT_EQ(success_items[0].position().container_guid(), container_guid);
  CASE_EXPECT_TRUE(success_items[0].position().grid_position().has_character_equipment());
}

// ============================================================
// 区域查询
// ============================================================

CASE_TEST(ItemContainer, get_entries_in_area_covers_multi_cell_items) {
  constexpr int64_t container_guid = 7401;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 4, 4, container_guid);
  auto config = make_test_config_group();

  // (0,0) 1x1×5, (1,1) 2x2, (3,3) 1x1×3
  ItemAddRequest add_requests;
  auto first_item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
  set_item_container_guid(first_item, container_guid);
  *add_requests.Add() = first_item;
  auto big_item = make_grid_item(kItemTypeId_2x2, 1, 1, 1);
  set_item_container_guid(big_item, container_guid);
  *add_requests.Add() = big_item;
  auto last_item = make_grid_item(kItemTypeId_1x1, 3, 3, 3);
  set_item_container_guid(last_item, container_guid);
  *add_requests.Add() = last_item;
  auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
  CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

  std::vector<item_entry_ptr_t> area_entries;

  // 区域 (0,0) 2x2: 命中 (0,0) 的 1x1 与 (1,1) 起的 2x2
  grid.get_entries_in_area(config, 0, 0, 2, 2, area_entries);
  CASE_EXPECT_EQ(area_entries.size(), static_cast<size_t>(2));
  if (area_entries.size() == 2) {
    CASE_EXPECT_EQ(area_entries[0]->item_instance().item_basic().type_id(), kItemTypeId_1x1);
    CASE_EXPECT_EQ(area_entries[1]->item_instance().item_basic().type_id(), kItemTypeId_2x2);
  }

  // 只覆盖 2x2 的非锚点格子: 按锚点查不到, 区域查询必须命中
  grid.get_entries_in_area(config, 2, 2, 1, 1, area_entries);
  CASE_EXPECT_EQ(area_entries.size(), static_cast<size_t>(1));
  if (area_entries.size() == 1) {
    CASE_EXPECT_EQ(area_entries[0]->item_instance().item_basic().type_id(), kItemTypeId_2x2);
  }
  CASE_EXPECT_TRUE(grid.get(make_inventory_target(2, 2).grid_position()) == nullptr);

  // 空白区域
  grid.get_entries_in_area(config, 0, 3, 1, 1, area_entries);
  CASE_EXPECT_TRUE(area_entries.empty());

  // 超出容器边界的区域: 只按相交判定, 不能越界读
  grid.get_entries_in_area(config, 3, 3, 4, 4, area_entries);
  CASE_EXPECT_EQ(area_entries.size(), static_cast<size_t>(1));
  if (area_entries.size() == 1) {
    CASE_EXPECT_EQ(area_entries[0]->item_instance().item_basic().count(), 3);
  }

  // 尺寸为 0 的区域
  grid.get_entries_in_area(config, 0, 0, 0, 2, area_entries);
  CASE_EXPECT_TRUE(area_entries.empty());
}

// ============================================================
// 操作来源透传 (check_* 传入的 ItemOperationSource 要随上下文到钩子)
// ============================================================

CASE_TEST(ItemContainer, operation_source_reaches_hooks) {
  constexpr int64_t container_guid = 7501;
  auto state = std::make_shared<HookTestState>();
  auto grid_ptr = atfw::util::memory::make_strong_rc<HookTestItemFiniteGridContainer>(state);
  auto& grid = *grid_ptr;
  register_test_log_handler(grid);
  grid.init(2, 2, PROJECT_NAMESPACE_ID::DItemGridPosition::kUserInventory, container_guid);
  grid.register_position_cfg(kItemTypeId_1x1, 99, 1, 1);
  grid.register_position_cfg(kItemTypeId_2x2, 1, 2, 2);
  auto config = make_test_config_group();

  const item_algorithm::ItemOperationSource add_source{1, 42, 7};
  const item_algorithm::ItemOperationSource sub_source{2, 43, 8};

  // add: 数量变化钩子收到的原因应是容器判定的 kAdd, 来源是 check_add 传入的那一份
  {
    ItemAddRequest add_requests;
    auto item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    set_item_container_guid(item, container_guid);
    *add_requests.Add() = item;

    auto checked = grid.check_add(config, make_item_readable_iterable(add_requests), add_source);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    state->last_count_context = item_algorithm::ItemOperationContext{};
    state->count_changed_calls = 0;

    CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(state->count_changed_calls, 1);
    CASE_EXPECT_TRUE(state->last_count_context.reason == item_algorithm::ItemOperationReason::kAdd);
    CASE_EXPECT_EQ(state->last_count_context.source.major_type, 1);
    CASE_EXPECT_EQ(state->last_count_context.source.minor_type, 42);
    CASE_EXPECT_EQ(state->last_count_context.source.micro_type, 7);
  }

  // sub: 同样要带上 check_sub 传入的来源, 原因换成 kSub
  {
    ItemSubRequest sub_requests;
    auto sub_basic = make_sub_basic(kItemTypeId_1x1, 2, 0, 0);
    set_basic_container_guid(sub_basic, container_guid);
    *sub_requests.Add() = sub_basic;

    auto checked = grid.check_sub(config, make_item_readable_iterable(sub_requests), sub_source);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    state->last_count_context = item_algorithm::ItemOperationContext{};
    state->count_changed_calls = 0;

    CASE_EXPECT_EQ(grid.sub(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(state->count_changed_calls, 1);
    CASE_EXPECT_TRUE(state->last_count_context.reason == item_algorithm::ItemOperationReason::kSub);
    CASE_EXPECT_EQ(state->last_count_context.source.major_type, 2);
    CASE_EXPECT_EQ(state->last_count_context.source.minor_type, 43);
    CASE_EXPECT_EQ(state->last_count_context.source.micro_type, 8);
  }

  // load: 不经 check_*, 来源为空, 原因固定为 kLoad
  {
    auto load_instance = make_grid_item(kItemTypeId_1x1, 3, 1, 1);
    set_item_container_guid(load_instance, container_guid);
    state->last_count_context = item_algorithm::ItemOperationContext{};
    state->count_changed_calls = 0;

    CASE_EXPECT_TRUE(grid.load(config, load_instance));
    CASE_EXPECT_EQ(state->count_changed_calls, 1);
    CASE_EXPECT_TRUE(state->last_count_context.reason == item_algorithm::ItemOperationReason::kLoad);
    CASE_EXPECT_EQ(state->last_count_context.source.major_type, 0);
    CASE_EXPECT_EQ(state->last_count_context.source.minor_type, 0);
    CASE_EXPECT_EQ(state->last_count_context.source.micro_type, 0);
  }
}

// ============================================================
// 遍历期间的写保护
// ============================================================

CASE_TEST(ItemContainer, foreach_instance_locks_container_against_mutation) {
  constexpr int64_t container_guid = 7502;
  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto& grid = *grid_ptr;
  init_test_container(grid, 2, 2, container_guid);
  auto config = make_test_config_group();

  {
    ItemAddRequest add_requests;
    auto item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    set_item_container_guid(item, container_guid);
    *add_requests.Add() = item;
    auto checked = grid.check_add(config, make_item_readable_iterable(add_requests));
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  size_t visited = 0;
  bool read_ok = false;
  bool add_rejected = false;
  bool sub_rejected = false;
  bool replace_rejected = false;
  bool load_rejected = false;
  bool inner_stopped = false;

  bool finished = grid.foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    ++visited;
    // 查询不受影响
    read_ok = grid.get_item_count(kItemTypeId_1x1) == 5 && nullptr != grid.find_entry(inst.item_basic());

    // check_* 只读, 仍可调用
    ItemAddRequest add_requests;
    auto add_item = make_grid_item(kItemTypeId_1x1, 1, 1, 1);
    set_item_container_guid(add_item, container_guid);
    *add_requests.Add() = add_item;
    auto add_checked = grid.check_add(config, make_item_readable_iterable(add_requests));
    add_rejected = add_checked.result.error_code == PROJECT_NAMESPACE_ID::EN_SUCCESS &&
                   PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM == grid.add(add_checked).error_code;

    ItemSubRequest sub_requests;
    auto sub_basic = make_sub_basic(kItemTypeId_1x1, 1, 0, 0);
    set_basic_container_guid(sub_basic, container_guid);
    *sub_requests.Add() = sub_basic;
    auto sub_checked = grid.check_sub(config, make_item_readable_iterable(sub_requests));
    sub_rejected = sub_checked.result.error_code == PROJECT_NAMESPACE_ID::EN_SUCCESS &&
                   PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM == grid.sub(sub_checked).error_code;

    ItemReplaceRequest replace_requests;
    auto replace_item = make_grid_item(kItemTypeId_1x1, 2, 0, 1);
    set_item_container_guid(replace_item, container_guid);
    *replace_requests.Add() = replace_item;
    auto replace_checked = grid.check_replace(config, make_item_readable_iterable(replace_requests));
    replace_rejected = replace_checked.result.error_code == PROJECT_NAMESPACE_ID::EN_SUCCESS &&
                       PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM == grid.replace(replace_checked).error_code;

    auto load_instance = make_grid_item(kItemTypeId_1x1, 1, 1, 0);
    set_item_container_guid(load_instance, container_guid);
    load_rejected = !grid.load(config, load_instance);

    // 嵌套遍历同样受限, 且回调返回 false 能中断
    inner_stopped = !grid.foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance&) { return false; });
    return true;
  });

  CASE_EXPECT_TRUE(finished);
  CASE_EXPECT_EQ(visited, static_cast<size_t>(1));
  CASE_EXPECT_TRUE(read_ok);
  CASE_EXPECT_TRUE(add_rejected);
  CASE_EXPECT_TRUE(sub_rejected);
  CASE_EXPECT_TRUE(replace_rejected);
  CASE_EXPECT_TRUE(load_rejected);
  CASE_EXPECT_TRUE(inner_stopped);

  // 遍历结束后容器恢复可写
  ItemAddRequest after_requests;
  auto after_item = make_grid_item(kItemTypeId_1x1, 1, 1, 1);
  set_item_container_guid(after_item, container_guid);
  *after_requests.Add() = after_item;
  auto after_checked = grid.check_add(config, make_item_readable_iterable(after_requests));
  CASE_EXPECT_EQ(after_checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  CASE_EXPECT_EQ(grid.add(after_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
}

// ============================================================
// 容器组 (ItemContainerGroup) — 按 position 路由 + 分片批量执行 + 跨容器移动
// ============================================================

/// @brief 测试用容器组: 按 position.container_guid() 从注册表里选容器
class TestItemContainerGroup : public item_algorithm::ItemContainerGroup {
 public:
  void register_container(int64_t container_guid, item_container_ptr_t container) {
    containers_[container_guid] = std::move(container);
  }

  item_container_ptr_t select_container(const PROJECT_NAMESPACE_ID::DItemPosition& position) override {
    return find_container(position.container_guid());
  }
  item_container_ptr_t select_container(const PROJECT_NAMESPACE_ID::DItemPosition& position) const override {
    return find_container(position.container_guid());
  }

 private:
  item_container_ptr_t find_container(int64_t container_guid) const {
    auto iter = containers_.find(container_guid);
    if (iter == containers_.end()) {
      return nullptr;
    }
    return iter->second;
  }

  std::unordered_map<int64_t, item_container_ptr_t> containers_;
};

/// @brief 容器内某类型的实时总数量 (遍历条目统计, 不依赖容器内部的数量缓存)
static int64_t count_instances_type(const ItemContainer& container, int32_t type_id) {
  int64_t total = 0;
  container.foreach_instance([&](const PROJECT_NAMESPACE_ID::DItemInstance& inst) {
    if (inst.item_basic().type_id() == type_id) {
      total += inst.item_basic().count();
    }
    return true;
  });
  return total;
}

/// @brief 组级 add/sub: 一次请求里包含多个容器的道具时按容器拆片, 各自独立执行
CASE_TEST(ItemContainerGroup, batch_add_sub_across_containers) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = make_test_config_group();

  constexpr int64_t kContainerGuidA = 0x5001;
  constexpr int64_t kContainerGuidB = 0x5002;
  constexpr int64_t kUnknownContainerGuid = 0x5003;

  TestItemContainerGroup group;
  auto grid_a = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto grid_b = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  init_test_container(*grid_a, 10, 10, kContainerGuidA);
  init_test_container(*grid_b, 10, 10, kContainerGuidB);
  group.register_container(kContainerGuidA, grid_a);
  group.register_container(kContainerGuidB, grid_b);

  // ============================================================
  // Case 1: 一次组级 add 里既有 A 容器的道具, 也有 B 容器的道具
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 组级 add 按容器拆片\n";
  {
    ItemAddRequest requests;
    {
      auto item = make_grid_item(kItemTypeId_1x1, 30, 0, 0);
      set_item_container_guid(item, kContainerGuidA);
      *requests.Add() = item;
    }
    {
      auto item = make_grid_item(kItemTypeId_2x2, 1, 2, 2);
      set_item_container_guid(item, kContainerGuidA);
      *requests.Add() = item;
    }
    {
      auto item = make_grid_item(kItemTypeId_1x1, 20, 1, 1);
      set_item_container_guid(item, kContainerGuidB);
      *requests.Add() = item;
    }

    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  // 每个容器只拿到属于自己的那部分
  CASE_EXPECT_EQ(30, count_instances_type(*grid_a, kItemTypeId_1x1));
  CASE_EXPECT_EQ(1, count_instances_type(*grid_a, kItemTypeId_2x2));
  CASE_EXPECT_EQ(0, count_instances_type(*grid_b, kItemTypeId_2x2));
  CASE_EXPECT_EQ(20, count_instances_type(*grid_b, kItemTypeId_1x1));

  // ============================================================
  // Case 2: 路由不到容器 → 组级直接失败, 不产生任何变更
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 路由不到容器时整体失败\n";
  {
    ItemAddRequest requests;
    auto item = make_grid_item(kItemTypeId_1x1, 5, 0, 0);
    set_item_container_guid(item, kUnknownContainerGuid);
    *requests.Add() = item;

    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kItemTypeId_1x1);
    // check 失败的 checked request 不能拿去执行
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  CASE_EXPECT_EQ(30, count_instances_type(*grid_a, kItemTypeId_1x1));

  // ============================================================
  // Case 3: 同一个容器内有一条不合法 (位置冲突) → 整体失败, 前面的容器也不改
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 某个容器 check 失败时整体失败\n";
  {
    ItemAddRequest requests;
    {
      // A 容器: (3,3) 放一个 2x2, 合法
      auto item = make_grid_item(kItemTypeId_2x2, 1, 3, 3);
      set_item_container_guid(item, kContainerGuidA);
      *requests.Add() = item;
    }
    {
      // B 容器: (2,2) 已经有 2x2, 再放一个会被占用挡住
      auto item = make_grid_item(kItemTypeId_2x2, 1, 2, 2);
      set_item_container_guid(item, kContainerGuidB);
      *requests.Add() = item;
    }

    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kItemTypeId_2x2);
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED);
  }
  // A 容器里仍然只有 Case 1 放进去的那一个 2x2, Case 3 那批没有落地
  CASE_EXPECT_EQ(1, count_instances_type(*grid_a, kItemTypeId_2x2));

  // ============================================================
  // Case 4: 组级 sub 一次扣两个容器
  // ============================================================
  CASE_MSG_INFO() << "Case 4: 组级 sub 跨容器扣减\n";
  {
    ItemSubRequest requests;
    {
      auto sub = make_sub_basic(kItemTypeId_1x1, 10, 0, 0);
      sub.mutable_position()->set_container_guid(kContainerGuidA);
      *requests.Add() = sub;
    }
    {
      auto sub = make_sub_basic(kItemTypeId_1x1, 5, 1, 1);
      sub.mutable_position()->set_container_guid(kContainerGuidB);
      *requests.Add() = sub;
    }

    auto checked = group.check_sub(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.sub(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(20, count_instances_type(*grid_a, kItemTypeId_1x1));
  CASE_EXPECT_EQ(15, count_instances_type(*grid_b, kItemTypeId_1x1));

  // ============================================================
  // Case 5: 组级 sub 数量不足 → 整体失败, 数据不动
  // ============================================================
  CASE_MSG_INFO() << "Case 5: 组级 sub 数量不足整体失败\n";
  {
    ItemSubRequest requests;
    auto sub = make_sub_basic(kItemTypeId_1x1, 21, 0, 0);
    sub.mutable_position()->set_container_guid(kContainerGuidA);
    *requests.Add() = sub;

    auto checked = group.check_sub(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kItemTypeId_1x1);
  }
  CASE_EXPECT_EQ(20, count_instances_type(*grid_a, kItemTypeId_1x1));
}

/// @brief 组级 move: 跨容器移动拆成 源容器 sub + 目标容器 add; 同容器换位只用一个分片
CASE_TEST(ItemContainerGroup, cross_container_move_and_reposition) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = make_test_config_group();

  constexpr int64_t kContainerGuidA = 0x5011;
  constexpr int64_t kContainerGuidB = 0x5012;

  TestItemContainerGroup group;
  auto grid_a = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto grid_b = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  init_test_container(*grid_a, 10, 10, kContainerGuidA);
  init_test_container(*grid_b, 10, 10, kContainerGuidB);
  group.register_container(kContainerGuidA, grid_a);
  group.register_container(kContainerGuidB, grid_b);

  // A 容器 (0,0) 先放 40 个 1x1
  {
    ItemAddRequest requests;
    auto item = make_grid_item(kItemTypeId_1x1, 40, 0, 0);
    set_item_container_guid(item, kContainerGuidA);
    *requests.Add() = item;
    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(40, count_instances_type(*grid_a, kItemTypeId_1x1));

  /// 构造一条组级移动请求: 从 from 容器的 (from_x, from_y) 取 count 个到 to 容器的 (to_x, to_y)
  auto make_group_move = [](int64_t from_guid, int32_t from_x, int32_t from_y, int64_t count, int64_t to_guid,
                            int32_t to_x, int32_t to_y) {
    std::vector<ItemContainerGroupMoveRequest> requests;
    ItemContainerGroupMoveRequest request;
    request.source_item_basic.set_type_id(kItemTypeId_1x1);
    request.source_item_basic.set_count(count);
    request.source_item_basic.mutable_position()->set_container_guid(from_guid);
    request.source_item_basic.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_x(from_x);
    request.source_item_basic.mutable_position()->mutable_grid_position()->mutable_user_inventory()->set_y(from_y);
    request.target_position.set_container_guid(to_guid);
    request.target_position.mutable_grid_position()->mutable_user_inventory()->set_x(to_x);
    request.target_position.mutable_grid_position()->mutable_user_inventory()->set_y(to_y);
    requests.push_back(std::move(request));
    return requests;
  };

  // ============================================================
  // Case 1: 跨容器移动 (A(0,0) 取 15 个到 B(3,3))
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 跨容器移动\n";
  {
    auto requests = make_group_move(kContainerGuidA, 0, 0, 15, kContainerGuidB, 3, 3);
    auto checked = group.check_move(config, std::move(requests));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.move(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(25, count_instances_type(*grid_a, kItemTypeId_1x1));
  CASE_EXPECT_EQ(15, count_instances_type(*grid_b, kItemTypeId_1x1));
  {
    auto dumped = dump_container_items(*grid_b);
    auto* moved = find_dumped_by_position(dumped, kItemTypeId_1x1, 3, 3);
    CASE_EXPECT_TRUE(moved != nullptr);
    if (moved) {
      CASE_EXPECT_EQ(15, moved->item_basic().count());
    }
  }

  // ============================================================
  // Case 2: 同容器换位 (A(0,0) 剩 25 个整体搬到 (5,5))
  // ============================================================
  CASE_MSG_INFO() << "Case 2: 同容器换位\n";
  {
    auto requests = make_group_move(kContainerGuidA, 0, 0, 25, kContainerGuidA, 5, 5);
    auto checked = group.check_move(config, std::move(requests));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.move(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(25, count_instances_type(*grid_a, kItemTypeId_1x1));
  {
    auto dumped = dump_container_items(*grid_a);
    CASE_EXPECT_TRUE(find_dumped_by_position(dumped, kItemTypeId_1x1, 0, 0) == nullptr);
    auto* moved = find_dumped_by_position(dumped, kItemTypeId_1x1, 5, 5);
    CASE_EXPECT_TRUE(moved != nullptr);
    if (moved) {
      CASE_EXPECT_EQ(25, moved->item_basic().count());
    }
  }

  // ============================================================
  // Case 3: 源条目不存在 → 组级失败
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 源条目不存在\n";
  {
    auto requests = make_group_move(kContainerGuidA, 7, 7, 1, kContainerGuidB, 0, 0);
    auto checked = group.check_move(config, std::move(requests));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kItemTypeId_1x1);
  }

  // ============================================================
  // Case 4: 移动数量超过持有量 → 组级失败
  // ============================================================
  CASE_MSG_INFO() << "Case 4: 移动数量超过持有量\n";
  {
    auto requests = make_group_move(kContainerGuidA, 5, 5, 26, kContainerGuidB, 0, 0);
    auto checked = group.check_move(config, std::move(requests));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kItemTypeId_1x1);
  }

  // ============================================================
  // Case 5: 多条组请求移动同一条目 → 数量合并到一次 sub
  // ============================================================
  CASE_MSG_INFO() << "Case 5: 同一条目的多条移动请求合并\n";
  {
    auto requests = make_group_move(kContainerGuidA, 5, 5, 10, kContainerGuidB, 6, 6);
    auto second = make_group_move(kContainerGuidA, 5, 5, 5, kContainerGuidB, 6, 6);
    requests.insert(requests.end(), second.begin(), second.end());

    auto checked = group.check_move(config, std::move(requests));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.move(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(10, count_instances_type(*grid_a, kItemTypeId_1x1));
  CASE_EXPECT_EQ(30, count_instances_type(*grid_b, kItemTypeId_1x1));
  {
    auto dumped = dump_container_items(*grid_b);
    auto* moved = find_dumped_by_position(dumped, kItemTypeId_1x1, 6, 6);
    CASE_EXPECT_TRUE(moved != nullptr);
    if (moved) {
      CASE_EXPECT_EQ(15, moved->item_basic().count());
    }
  }

  // ============================================================
  // Case 6: 目标位置被不同类型占据 → 组级失败, 数据不动
  // ============================================================
  CASE_MSG_INFO() << "Case 6: 目标位置被不同类型占据\n";
  {
    // B 容器 (1,1) 放一个 2x2 (不同类型, 无法合入)
    ItemAddRequest pre_requests;
    auto big_item = make_grid_item(kItemTypeId_2x2, 1, 1, 1);
    set_item_container_guid(big_item, kContainerGuidB);
    *pre_requests.Add() = big_item;
    auto pre_checked = group.check_add(config, pre_requests);
    CASE_EXPECT_EQ(pre_checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.add(pre_checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);

    // 从 A(5,5) 取 1 个 1x1 移到 B(1,1): B(1,1) 已被 2x2 占据, 返回 TARGET_OCCUPIED
    auto requests = make_group_move(kContainerGuidA, 5, 5, 1, kContainerGuidB, 1, 1);
    auto checked = group.check_move(config, std::move(requests));
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_MOVE_TARGET_OCCUPIED);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kItemTypeId_1x1);
    // check 不通过时不能执行
    CASE_EXPECT_NE(group.move(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 数据不变
  CASE_EXPECT_EQ(10, count_instances_type(*grid_a, kItemTypeId_1x1));
  CASE_EXPECT_EQ(30, count_instances_type(*grid_b, kItemTypeId_1x1));
  CASE_EXPECT_EQ(1, count_instances_type(*grid_b, kItemTypeId_2x2));
}

/// @brief 组级 check_has / replace: 按容器分片后各自检查 / 替换
CASE_TEST(ItemContainerGroup, has_and_replace_across_containers) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = make_test_config_group();

  constexpr int64_t kContainerGuidA = 0x5021;
  constexpr int64_t kContainerGuidB = 0x5022;

  TestItemContainerGroup group;
  auto grid_a = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  auto grid_b = atfw::util::memory::make_strong_rc<TestItemFiniteGridContainer>();
  init_test_container(*grid_a, 10, 10, kContainerGuidA);
  init_test_container(*grid_b, 10, 10, kContainerGuidB);
  group.register_container(kContainerGuidA, grid_a);
  group.register_container(kContainerGuidB, grid_b);

  // 两个容器各放一条
  {
    ItemAddRequest requests;
    {
      auto item = make_grid_item(kItemTypeId_1x1, 10, 0, 0);
      set_item_container_guid(item, kContainerGuidA);
      *requests.Add() = item;
    }
    {
      auto item = make_grid_item(kItemTypeId_1x1, 6, 1, 1);
      set_item_container_guid(item, kContainerGuidB);
      *requests.Add() = item;
    }
    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  // ============================================================
  // Case 1: check_has 跨容器对账 (都不超过持有量)
  // ============================================================
  CASE_MSG_INFO() << "Case 1: check_has 跨容器通过\n";
  {
    ItemHasRequest requests;
    auto basic = make_sub_basic(kItemTypeId_1x1, 10, 0, 0);
    basic.mutable_position()->set_container_guid(kContainerGuidA);
    *requests.Add() = basic;
    auto basic_b = make_sub_basic(kItemTypeId_1x1, 6, 1, 1);
    basic_b.mutable_position()->set_container_guid(kContainerGuidB);
    *requests.Add() = basic_b;

    auto result = group.check_has(config, requests);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  // ============================================================
  // Case 2: 某个容器数量不足 → 整体失败
  // ============================================================
  CASE_MSG_INFO() << "Case 2: check_has 数量不足整体失败\n";
  {
    ItemHasRequest requests;
    auto basic = make_sub_basic(kItemTypeId_1x1, 11, 0, 0);
    basic.mutable_position()->set_container_guid(kContainerGuidA);
    *requests.Add() = basic;

    auto result = group.check_has(config, requests);
    CASE_EXPECT_EQ(result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(result.failed_type_id, kItemTypeId_1x1);
  }

  // ============================================================
  // Case 3: 组级 replace 只替换指定容器的内容
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 组级 replace\n";
  {
    ItemReplaceRequest requests;
    auto item = make_grid_item(kItemTypeId_2x2, 1, 4, 4);
    set_item_container_guid(item, kContainerGuidB);
    *requests.Add() = item;

    auto checked = group.check_replace(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.replace(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // B 被整体换成新列表, A 不受影响
  CASE_EXPECT_EQ(0, count_instances_type(*grid_b, kItemTypeId_1x1));
  CASE_EXPECT_EQ(1, count_instances_type(*grid_b, kItemTypeId_2x2));
  CASE_EXPECT_EQ(10, count_instances_type(*grid_a, kItemTypeId_1x1));
}

/// @brief 无位置容器不接入 move: 基类默认钩子直接拒绝, 数据不变
CASE_TEST(ItemContainerGroup, no_position_container_rejects_move) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = make_test_config_group();

  constexpr int64_t kContainerGuid = 0x5031;

  auto grid_ptr = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  auto& grid = *grid_ptr;
  grid.init(PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, kContainerGuid);
  register_test_log_handler(grid);

  // 先放 10 个不占格道具
  {
    ItemAddRequest requests;
    auto item = make_ungrid_item(kCoinTypeId, 10);
    set_item_container_guid(item, kContainerGuid);
    *requests.Add() = item;

    auto checked = grid.check_add(config, requests);
    CASE_EXPECT_EQ(checked.result.error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(grid.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(10, count_instances_type(grid, kCoinTypeId));

  // 用真实条目构造移动请求: 即使条目存在, 本模式也不支持 move
  ItemMoveRequest move_request;
  {
    item_entry_ptr_t entry = grid.find_entry(make_sub_basic(kCoinTypeId, 10));
    CASE_EXPECT_TRUE(!!entry);
    if (entry) {
      ItemMoveSubRequest sub_request;
      sub_request.entry = entry;
      sub_request.op_count = 10;
      move_request.move_sub_entrys.push_back(std::move(sub_request));
    }
  }

  auto checked_move = grid.check_move(config, std::move(move_request));
  CASE_EXPECT_EQ(checked_move.result.error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  // check 失败后执行也会被挡下, 数据保持不变
  CASE_EXPECT_EQ(grid.move(checked_move).error_code, PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  CASE_EXPECT_EQ(10, count_instances_type(grid, kCoinTypeId));
}

/// @brief 无位置容器 (虚拟仓库) 组级批量增删: 同类型合入, count==0 跳过, 数量不足失败
CASE_TEST(ItemContainerGroup, no_position_batch_add_sub_and_empty_request) {
  using namespace item_algorithm;  // NOLINT(build/namespaces)
  auto config = make_test_config_group();

  constexpr int64_t kContainerGuidA = 0x5041;
  constexpr int64_t kContainerGuidB = 0x5042;

  TestItemContainerGroup group;
  auto grid_a = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  auto grid_b = atfw::util::memory::make_strong_rc<TestItemNoPositionContainer>();
  grid_a->init(PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, kContainerGuidA);
  grid_b->init(PROJECT_NAMESPACE_ID::DItemGridPosition::kVirtualInventory, kContainerGuidB);
  register_test_log_handler(*grid_a);
  register_test_log_handler(*grid_b);
  group.register_container(kContainerGuidA, grid_a);
  group.register_container(kContainerGuidB, grid_b);

  // ============================================================
  // Case 1: 批量添加: 同类型两条 + 另一种类型一条, 按容器拆片
  // ============================================================
  CASE_MSG_INFO() << "Case 1: 无位置批量添加 (同类型合入 + 跨容器拆片)\n";
  {
    ItemAddRequest requests;
    {
      auto item = make_ungrid_item(kCoinTypeId, 100);
      set_item_container_guid(item, kContainerGuidA);
      *requests.Add() = item;
    }
    {
      auto item = make_ungrid_item(kCoinTypeId, 50);
      set_item_container_guid(item, kContainerGuidA);
      *requests.Add() = item;
    }
    {
      auto item = make_ungrid_item(kVirtualTypeId, 7);
      set_item_container_guid(item, kContainerGuidB);
      *requests.Add() = item;
    }

    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  // 同类型合入一条 (100+50=150), 跨容器各自独立
  CASE_EXPECT_EQ(150, count_instances_type(*grid_a, kCoinTypeId));
  CASE_EXPECT_EQ(0, count_instances_type(*grid_a, kVirtualTypeId));
  CASE_EXPECT_EQ(7, count_instances_type(*grid_b, kVirtualTypeId));
  CASE_EXPECT_EQ(0, count_instances_type(*grid_b, kCoinTypeId));

  // ============================================================
  // Case 2: count == 0 的空请求: check 与执行都跳过, 数量不变
  // ============================================================
  CASE_MSG_INFO() << "Case 2: count==0 空请求跳过\n";
  {
    ItemAddRequest requests;
    auto item = make_ungrid_item(kCoinTypeId, 0);
    set_item_container_guid(item, kContainerGuidA);
    *requests.Add() = item;

    auto checked = group.check_add(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.add(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(150, count_instances_type(*grid_a, kCoinTypeId));

  // ============================================================
  // Case 3: 批量删除: 一次扣两种类型 (跨容器)
  // ============================================================
  CASE_MSG_INFO() << "Case 3: 无位置批量删除 (跨容器)\n";
  {
    ItemSubRequest requests;
    {
      auto sub = make_sub_basic(kCoinTypeId, 120);
      set_basic_container_guid(sub, kContainerGuidA);
      *requests.Add() = sub;
    }
    {
      auto sub = make_sub_basic(kVirtualTypeId, 7);
      set_basic_container_guid(sub, kContainerGuidB);
      *requests.Add() = sub;
    }

    auto checked = group.check_sub(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_SUCCESS);
    CASE_EXPECT_EQ(group.sub(checked).error_code, PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }
  CASE_EXPECT_EQ(30, count_instances_type(*grid_a, kCoinTypeId));
  CASE_EXPECT_EQ(0, count_instances_type(*grid_b, kVirtualTypeId));

  // ============================================================
  // Case 4: 数量不足: check_sub 直接失败, 数据不动
  // ============================================================
  CASE_MSG_INFO() << "Case 4: 数量不足整体失败\n";
  {
    ItemSubRequest requests;
    auto sub = make_sub_basic(kCoinTypeId, 31);
    set_basic_container_guid(sub, kContainerGuidA);
    *requests.Add() = sub;

    auto checked = group.check_sub(config, requests);
    CASE_EXPECT_EQ(checked.get_error_code(), PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH);
    CASE_EXPECT_EQ(checked.get_failed_type_id(), kCoinTypeId);
  }
  CASE_EXPECT_EQ(30, count_instances_type(*grid_a, kCoinTypeId));
}
