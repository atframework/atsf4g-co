// Copyright 2026 atframework

#include <ItemAlgorithm/ItemContainer.h>

#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#  include <intrin.h>
#endif

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 生命周期与初始化
// ============================================================

ITEM_ALGORITHM_API ItemContainer::ItemContainer()
    : init_(false), container_guid_(0), next_entry_id_(1), operate_id_(0) {}

ITEM_ALGORITHM_API ItemContainer::~ItemContainer() {}

ITEM_ALGORITHM_API void ItemContainer::init_container(int64_t container_guid) {
  init_ = true;
  container_guid_ = container_guid;
  clear();

  FWINSTLOGDEBUG(logger(), "init container container_guid={}", container_guid_);
}

ITEM_ALGORITHM_API void ItemContainer::clear() {
  if (!is_operation_allowed("ItemContainer::clear")) {
    return;
  }

  // clear 与 sub 的整体移除走同一套变更流程, 区别只是不做任何校验 (调用方已经决定要清空):
  // 先摘掉模式自己的索引, 再逐条摘分组 / 数量归零, 并触发
  // on_item_count_changed (new_count == 0) 与 on_item_data_changed, 让上层同步感知被清掉的条目。
  const ItemOperationContext context{ItemOperationReason::kClear, ItemOperationSource{}};

  // 通知回调可能改写容器, 先把待清空的条目快照出来 (remove_entry_from_group 会改动分组)
  std::vector<item_entry_ptr_t> all_entries;
  all_entries.reserve(get_entry_count());
  for (const auto& group_pair : item_groups_) {
    for (const auto& entry : group_pair.second.entries) {
      if (entry) {
        all_entries.push_back(entry);
      }
    }
  }

  // 先摘索引 (位置 / GUID / 位图), 与 sub 完全移除时的顺序一致
  on_clear();

  size_t removed_count = 0;
  for (const auto& entry : all_entries) {
    int64_t old_count = entry->item_instance().item_basic().count();
    if (old_count <= 0) {
      continue;
    }
    // 与 sub 的完全移除同序: 摘分组 -> 数量归零 -> 通知
    remove_entry_from_group(entry);
    set_entry_count(entry, 0);
    notify_entry_count_changed(entry, old_count, 0, context);
    ++removed_count;
  }

  // 快照里的条目应已逐条摘除, 基类里不应再有数据。
  // 仍有残留说明 entries 与分组的对应关系被破坏 (例如遍历期间改写了 type_id), 记 ERROR 便于定位。
  if (!item_groups_.empty()) {
    FWINSTLOGERROR(logger(),
                   "clear container leftover groups after per-entry removal, container_guid={}, leftover_groups={}",
                   container_guid_, item_groups_.size());
  }

  // 兜底清理 (通知期间产生的空条目 / 数量项) 与 entry_id 索引
  item_groups_.clear();
  if (!entry_id_index_.empty()) {
    FWINSTLOGERROR(logger(),
                   "clear container leftover entry_id index after per-entry removal, container_guid={}, leftover={}",
                   container_guid_, entry_id_index_.size());
  }
  entry_id_index_.clear();

  FWINSTLOGDEBUG(logger(), "clear container container_guid={}, {} entries removed", container_guid_, removed_count);
}

ITEM_ALGORITHM_API void ItemContainer::on_clear() {}

ITEM_ALGORITHM_API bool ItemContainer::foreach_instance(
    atfw::util::nostd::function_ref<bool(const PROJECT_NAMESPACE_ID::DItemInstance&)> fn) const {
  struct iteration_guard_t {
    const ItemContainer* self;
    explicit iteration_guard_t(const ItemContainer* in_self) noexcept : self(in_self) { ++self->iteration_depth_; }
    ~iteration_guard_t() { --self->iteration_depth_; }
  } guard(this);

  for (const auto& group_pair : item_groups_) {
    for (const auto& entry : group_pair.second.entries) {
      if (entry && !fn(entry->item_instance())) {
        return false;
      }
    }
  }
  return true;
}

// ============================================================
// 查询接口
// ============================================================

ITEM_ALGORITHM_API const ItemContainer::item_group_type* ItemContainer::get_group(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  if (it != item_groups_.end()) {
    return &it->second.entries;
  }
  return nullptr;
}

ITEM_ALGORITHM_API int64_t ItemContainer::get_item_count_debug(int32_t type_id) const {
  return count_type_total(type_id);
}

ITEM_ALGORITHM_API int64_t ItemContainer::get_item_count(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  return (it != item_groups_.end()) ? it->second.count_cache : 0;
}

ITEM_ALGORITHM_API bool ItemContainer::is_empty() const { return item_groups_.empty(); }

ITEM_ALGORITHM_API const ItemContainer::item_group_map_type& ItemContainer::get_all_groups() const {
  return item_groups_;
}

ITEM_ALGORITHM_API int64_t ItemContainer::get_container_guid() const { return container_guid_; }

ITEM_ALGORITHM_API size_t ItemContainer::get_entry_count() const { return entry_id_index_.size(); }

// ============================================================
// 日志接口
// ============================================================

ITEM_ALGORITHM_API void ItemContainer::set_log_handler(const ItemLogHandler& handler) {
  // category 是视图, 复制到容器自己的存储后再让日志处理器指过去
  log_category_storage_.assign(handler.category.data(), handler.category.size());

  log_handler_ = handler;
  log_handler_.category = log_category_storage_;
}

ITEM_ALGORITHM_API const ItemLogHandler& ItemContainer::get_log_handler() const { return log_handler_; }

ITEM_ALGORITHM_API bool ItemContainer::is_operation_allowed(const char* operation_name) const {
  if (iteration_depth_ == 0) {
    return true;
  }

  FWINSTLOGERROR(logger(), "{} is not allowed while iterating the container, container_guid={}", operation_name,
                 container_guid_);
  return false;
}

// ============================================================
// 位置读取 — 默认实现
// ============================================================

ITEM_ALGORITHM_API item_entry_ptr_t ItemContainer::find_entry_at_position(const ItemGridPosition& /*position*/) const {
  return nullptr;
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemContainer::find_entry_by_guid(int64_t /*guid*/) const { return nullptr; }

ITEM_ALGORITHM_API bool ItemContainer::has_entry_guid(int64_t /*guid*/) const {
  // 默认没有 GUID 索引: 不接受 GUID 的模式 (无位置) 不用覆盖
  return false;
}

// ============================================================
// 变更通知钩子 — 默认实现
// ============================================================

ITEM_ALGORITHM_API int32_t ItemContainer::on_check_add_one(
    const excel_config_group_ptr_t& /*config_group*/, const PROJECT_NAMESPACE_ID::DItemInstance& /*request*/) const {
  // 默认不限制, 业务按需覆盖
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API int32_t ItemContainer::on_check_sub_one(const excel_config_group_ptr_t& /*config_group*/,
                                                           const PROJECT_NAMESPACE_ID::DItemBasic& /*request*/) const {
  // 默认不限制, 业务按需覆盖
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API int32_t ItemContainer::on_check_has_one(const excel_config_group_ptr_t& /*config_group*/,
                                                           const PROJECT_NAMESPACE_ID::DItemBasic& /*request*/) const {
  // 默认不限制, 业务按需覆盖
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API int32_t ItemContainer::on_check_item_count_limit(int32_t /*type_id*/, int64_t /*current_count*/,
                                                                    int64_t /*add_count*/) const {
  // 默认不限制, 业务按需覆盖
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API bool ItemContainer::should_skip_add_request(
    const PROJECT_NAMESPACE_ID::DItemInstance& /*request*/) const {
  // 默认不跳过, 业务按需覆盖
  return false;
}

ITEM_ALGORITHM_API void ItemContainer::on_item_count_changed(int32_t /*type_id*/, const item_entry_ptr_t& /*entry*/,
                                                             int64_t /*guid*/, const ItemGridPosition& /*position*/,
                                                             int64_t /*old_count*/, int64_t /*new_count*/,
                                                             int64_t /*type_total_count*/,
                                                             const ItemOperationContext& /*context*/) {
  // 默认空实现, 业务按需覆盖
}

ITEM_ALGORITHM_API bool ItemContainer::check_item_position(
    const PROJECT_NAMESPACE_ID::DItemPosition& /*position*/) const {
  // 默认不检查, 业务按需覆盖
  return true;
}

ITEM_ALGORITHM_API const PROJECT_NAMESPACE_ID::DItemPositionCfg* ItemContainer::get_item_position_cfg(
    const excel_config_group_ptr_t& config_group, const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
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

ITEM_ALGORITHM_API void ItemContainer::on_item_data_changed(const item_entry_ptr_t& /*entry*/,
                                                            const ItemOperationContext& /*context*/) {
  // 默认空实现, 业务按需覆盖
}

// ============================================================
// 条目查询
// ============================================================

ITEM_ALGORITHM_API item_entry_ptr_t ItemContainer::find_entry(const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  int64_t guid = basic.guid();

  if (guid != 0) {
    return find_entry_by_guid(guid);
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
  if (item_type_config == nullptr) {
    return nullptr;
  }

  if (item_type_config->need_occupy_the_grid) {
    // 占格道具: 按位置查找
    return find_entry_at_position(extract_position(basic.position().grid_position()));
  }

  // 无位置道具: 按类型查找
  auto it = item_groups_.find(basic.type_id());
  if (it != item_groups_.end() && !it->second.entries.empty()) {
    return *it->second.entries.begin();
  }
  return nullptr;
}

ITEM_ALGORITHM_API bool ItemContainer::is_same_grid_position(const PROJECT_NAMESPACE_ID::DItemGridPosition& lhs,
                                                            const PROJECT_NAMESPACE_ID::DItemGridPosition& rhs) const {
  return extract_position(lhs) == extract_position(rhs);
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemContainer::find_entry_by_id(uint64_t entry_id) const {
  auto it = entry_id_index_.find(entry_id);
  if (it != entry_id_index_.end()) {
    return it->second.lock();
  }
  return nullptr;
}

// ============================================================
// 内部实现
// ============================================================

void ItemContainer::remove_entry_from_group(const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  int32_t type_id = entry->item_instance().item_basic().type_id();
  auto group_it = item_groups_.find(type_id);
  if (group_it != item_groups_.end()) {
    group_it->second.entries.erase(entry);
    // 条目与数量缓存都空了才回收整个分组, 避免留下空壳
    if (group_it->second.entries.empty() && group_it->second.count_cache <= 0) {
      item_groups_.erase(group_it);
    }
  }
}

bool ItemContainer::insert_entry_into_group(const item_entry_ptr_t& entry) {
  if (!entry) {
    return false;
  }
  return item_groups_[entry->item_instance().item_basic().type_id()].entries.insert(entry).second;
}

int64_t ItemContainer::change_cached_item_count(int32_t type_id, int64_t delta) {
  auto& group = item_groups_[type_id];
  group.count_cache += delta;
  if (group.count_cache <= 0) {
    group.count_cache = 0;
    if (group.entries.empty()) {
      item_groups_.erase(type_id);
    }
    return 0;
  }
  return group.count_cache;
}

void ItemContainer::add_entry_id_index(const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }
  entry_id_index_[entry->entry_id()] = entry;
}

void ItemContainer::remove_entry_id_index(uint64_t entry_id) { entry_id_index_.erase(entry_id); }

ITEM_ALGORITHM_API uint64_t
ItemContainer::make_entry_sort_key(const PROJECT_NAMESPACE_ID::DItemInstance& /*instance*/) const {
  // 基类不关心位置, 默认键为 0; 跟踪位置的模式覆盖本函数
  return 0;
}

ITEM_ALGORITHM_API void ItemContainer::set_entry_sort_key(const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }
  entry->set_sort_key(make_entry_sort_key(entry->item_instance()));
}

ITEM_ALGORITHM_API void ItemContainer::set_entry_count(const item_entry_ptr_t& entry, int64_t count) {
  if (!entry) {
    return;
  }
  entry->mutable_item_basic().set_count(count);
}

ITEM_ALGORITHM_API void ItemContainer::set_entry_instance(const item_entry_ptr_t& entry,
                                                          const PROJECT_NAMESPACE_ID::DItemInstance& instance) {
  if (!entry) {
    return;
  }
  entry->mutable_item_instance() = instance;
}

item_entry_ptr_t ItemContainer::make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance) {
  auto entry = atfw::util::memory::make_strong_rc<ItemEntry>(shared_from_this(), std::move(instance), next_entry_id_++);
  add_entry_id_index(entry);
  set_entry_sort_key(entry);
  return entry;
}

item_entry_ptr_t ItemContainer::make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance, uint64_t entry_id) {
  auto entry = atfw::util::memory::make_strong_rc<ItemEntry>(shared_from_this(), std::move(instance), entry_id);
  add_entry_id_index(entry);
  set_entry_sort_key(entry);
  return entry;
}

// ============================================================
// 批次校验的公共单条检查与公共收尾
// ============================================================

ITEM_ALGORITHM_API int64_t ItemContainer::count_type_total(int32_t type_id) const {
  auto it = item_groups_.find(type_id);
  if (it == item_groups_.end()) {
    return 0;
  }

  int64_t total = 0;
  for (const auto& entry : it->second.entries) {
    if (entry) {
      total += entry->item_instance().item_basic().count();
    }
  }
  return total;
}

ITEM_ALGORITHM_API int32_t ItemContainer::validate_item_basic(const excel_config_group_ptr_t& config_group,
                                                              const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  if (!is_item_valid(config_group, basic)) {
    return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  if (basic.position().container_guid() != get_container_guid()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  if (!check_item_position(basic.position())) {
    return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  if (ItemAlgorithmTypeOption::GetItemType(basic.type_id()) == nullptr) {
    return PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
  }
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::validate_checked_request(const ItemOperationResult& check_result,
                                                                               bool apply, int64_t container_guid,
                                                                               int64_t operate_id,
                                                                               const char* operation_name) {
  if (check_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWINSTLOGERROR(logger(), "{} called with failed checked request, error={} ({})", operation_name,
                   check_result.error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(check_result.error_code));
    return check_result;
  }
  if (apply) {
    FWINSTLOGERROR(logger(), "{} called with apply=true, should not happen", operation_name);
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  if (container_guid != get_container_guid()) {
    FWINSTLOGERROR(logger(), "{} called with container_guid={} but container_guid={} mismatch", operation_name,
                   container_guid, get_container_guid());
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  if (operate_id != get_operate_id()) {
    FWINSTLOGERROR(logger(), "{} called with operate_id={} but operate_id={} mismatch", operation_name, operate_id,
                   get_operate_id());
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
}

ITEM_ALGORITHM_API int64_t ItemContainer::notify_entry_count_changed(const item_entry_ptr_t& entry, int64_t old_count,
                                                                     int64_t new_count,
                                                                     const ItemOperationContext& context) {
  if (!entry) {
    return 0;
  }

  const auto& basic = entry->item_instance().item_basic();
  int32_t type_id = basic.type_id();
  ItemGridPosition position = extract_position(basic.position().grid_position());

  int64_t total_count = get_item_count(type_id);
  if (new_count != old_count) {
    total_count = change_cached_item_count(type_id, new_count - old_count);
  }
  on_item_count_changed(type_id, entry, basic.guid(), position, old_count, new_count, total_count, context);
  on_item_data_changed(entry, context);
  return total_count;
}

ITEM_ALGORITHM_API void ItemContainer::notify_entry_data_changed(const item_entry_ptr_t& entry,
                                                                 const ItemOperationContext& context) {
  if (!entry) {
    return;
  }

  on_item_data_changed(entry, context);
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
