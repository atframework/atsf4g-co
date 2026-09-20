// Copyright 2026 atframework

#include <ItemAlgorithm/Container/ItemNoPositionContainer.h>

#include <config/excel/item_type_config.h>

#include <algorithm>
#include <utility>
#include <vector>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 无位置容器的单条操作实现
//
// 增删改查的批次流程 (check_add / add / check_sub / sub / check_has /
// load / apply_entries / find_positions_*) 都在基类 ItemContainer 里, 本文件只实现基类路由过来的
// 单条钩子。
//
// 本模式不关心位置: 道具按类型合并计数, 没有位置索引、没有 GUID、没有格子与占用判断,
// 也没有 move (无从谈起把道具从哪个位置挪到哪个位置)。
// 同类型可能存在多个条目 (客户端同步会新建条目), 因此按类型扣减时要逐个扣到满足为止。
// 位置字段的读写 (extract_position / apply_position) 由接入层实现。
// ============================================================

// ============================================================
// 批次校验钩子
//
// 本模式的规则只有 is_item_valid (不占格 + 无 GUID), 基类的 validate_item_basic 已经调过;
// GUID 唯一、数量上限、按类型总数对账也都在基类, 三个整批钩子都没有可补的规则。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult
ItemNoPositionContainer::on_check_add(const ItemAddCheckedRequest& /*checked_request*/) const {
  // 被调用时机: 基类 check_add 完成通用字段校验后。本模式没有额外规则。
  return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
}

// ============================================================
// 单条放入
//
// 同类型已有条目就合入该条目, 否则新建条目。本模式没有堆叠上限。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult ItemNoPositionContainer::on_add_one(
    ItemAddCheckedRequest& /*checked_request*/, const PROJECT_NAMESPACE_ID::DItemInstance& req,
    const ItemOperationContext& context) {
  // 被调用时机: 基类 add 逐条循环, 每条一次; load 也走这里 (载入时同样要落位)。
  ItemOperationResult result;

  const auto& item_basic = req.item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t add_count = item_basic.count();
  int64_t guid = item_basic.guid();

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr || item_type_config->need_occupy_the_grid) {
    // 无位置容器只接受不占格道具 (is_item_valid 已挡住, 这里兜底)
    FWINSTLOGERROR(logger(), "add failed: item type={} is not allowed in no position container", type_id);
    return result;
  }
  if (guid != 0) {
    FWINSTLOGERROR(logger(), "add failed: no position container does not accept guid, type={} guid={}", type_id, guid);
    return result;
  }

  // 本模式同类型只保留一个"计数条目": 已有条目直接合入, 不做堆叠上限判断
  const item_group_type* group = get_group(type_id);
  if (group != nullptr && !group->empty()) {
    item_entry_ptr_t existing = *group->begin();
    int64_t old_count = existing->item_instance().item_basic().count();
    set_entry_count(existing, old_count + add_count);
    notify_entry_count_changed(existing, old_count, old_count + add_count, context);
    FWINSTLOGDEBUG(logger(), "add merge type={} count={} entry_id={} total={}", type_id, add_count,
                   existing->entry_id(), old_count + add_count);
    return result;
  }

  // 首次添加: 新建条目 (请求是只读视图, 拷一份再落位)
  PROJECT_NAMESPACE_ID::DItemInstance new_instance = req;
  item_entry_ptr_t entry = make_entry(std::move(new_instance));
  if (!insert_entry_into_group(entry)) {
    FWINSTLOGERROR(logger(), "[EntrySortKeyError] add failed to insert entry type={} count={} entry_id={}", type_id,
                   add_count, entry->entry_id());
    // 条目没进分组, 不能再动数量缓存, 否则缓存与实时计数会对不上
    return result;
  }
  notify_entry_count_changed(entry, 0, add_count, context);
  FWINSTLOGDEBUG(logger(), "add new type={} count={} entry_id={}", type_id, add_count, entry->entry_id());

  return result;
}

// ============================================================
// 批次校验钩子 (扣减)
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult
ItemNoPositionContainer::on_check_sub(const ItemSubCheckedRequest& /*checked_request*/) const {
  // 被调用时机: 基类 check_sub 完成通用字段与按类型总数校验后。本模式没有额外规则。
  return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
}

// ============================================================
// 单条扣减
//
// 只按类型扣减: 同类型可能有多个条目, 逐个扣到满足为止, 扣光的条目直接从分组移除。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult ItemNoPositionContainer::on_sub_one(ItemSubCheckedRequest& /*checked_request*/,
                                                                           const PROJECT_NAMESPACE_ID::DItemBasic& req,
                                                                           const ItemOperationContext& context) {
  // 被调用时机: 基类 sub 逐条循环, 每条一次。
  ItemOperationResult result;

  int64_t sub_count = req.count();
  int32_t type_id = req.type_id();

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr || item_type_config->need_occupy_the_grid || req.guid() != 0) {
    // check_sub 已保证类型存在且不占格, 这里兜底避免配置被改动后走到不该走的分支
    FWINSTLOGERROR(logger(), "sub failed: invalid item type={} guid={}, skip", type_id, req.guid());
    return result;
  }

  const item_group_type* group = get_group(type_id);
  if (group == nullptr || group->empty()) {
    FWINSTLOGERROR(logger(), "sub failed: type={} group empty, skip", type_id);
    return result;
  }

  // remove_entry_from_group 会改动集合, 先把条目快照出来再逐个扣
  std::vector<item_entry_ptr_t> type_entries;
  type_entries.reserve(group->size());
  for (const auto& type_entry : *group) {
    if (type_entry) {
      type_entries.push_back(type_entry);
    }
  }

  int64_t remaining = sub_count;
  for (const auto& type_entry : type_entries) {
    if (remaining <= 0) {
      break;
    }

    int64_t current_count = type_entry->item_instance().item_basic().count();
    if (current_count <= 0) {
      continue;
    }

    int64_t take_count = std::min(current_count, remaining);
    if (take_count == current_count) {
      // 该条目被扣光: 从分组移除并归零
      remove_entry_from_group(type_entry);
      set_entry_count(type_entry, 0);
    } else {
      set_entry_count(type_entry, current_count - take_count);
    }

    notify_entry_count_changed(type_entry, current_count, current_count - take_count, context);
    FWINSTLOGDEBUG(logger(), "sub type={} count={} entry_id={} remaining={}", type_id, take_count,
                   type_entry->entry_id(), current_count - take_count);
    remaining -= take_count;
  }

  if (remaining > 0) {
    FWINSTLOGERROR(logger(), "sub failed: type={} not enough, remaining={} not deducted", type_id, remaining);
  }

  return result;
}

// ============================================================
// 批次校验钩子 (对账)
//
// 只按类型总数对账, 本模式没有位置与 GUID 可查。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult ItemNoPositionContainer::on_check_has(
    const excel_config_group_ptr_t& /*config_group*/, const item_basic_readable_iterable& /*requests*/) const {
  // 被调用时机: 基类 check_has 完成通用字段与按类型总数校验后。本模式没有额外规则。
  return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
}

// ============================================================
// 载入
//
// 初始化后按条目载入: 同类型已有条目时合入, 否则新建。本模式没有堆叠上限。
// ============================================================

ITEM_ALGORITHM_API bool ItemNoPositionContainer::on_load_one(const excel_config_group_ptr_t& /*config_group*/,
                                                             const PROJECT_NAMESPACE_ID::DItemInstance& item_instance,
                                                             const ItemOperationContext& context) {
  // 被调用时机: 基类 load 通过 (遍历保护 / 初始化 / 字段校验 / 容器归属 / 数量上限) 之后一次。
  const auto& item_basic = item_instance.item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t add_count = item_basic.count();

  // 合入已有条目
  const item_group_type* group = get_group(type_id);
  if (group != nullptr && !group->empty()) {
    item_entry_ptr_t existing = *group->begin();
    int64_t old_count = existing->item_instance().item_basic().count();
    set_entry_count(existing, old_count + add_count);
    int64_t total_count = notify_entry_count_changed(existing, old_count, old_count + add_count, context);
    FWINSTLOGINFO(logger(), "load success: merge type={} count={} entry_id={} total={}", type_id, add_count,
                  existing->entry_id(), total_count);
    return true;
  }

  // 首次添加: 新建条目
  PROJECT_NAMESPACE_ID::DItemInstance new_instance = item_instance;
  item_entry_ptr_t entry = make_entry(std::move(new_instance));
  if (!insert_entry_into_group(entry)) {
    FWINSTLOGERROR(logger(), "[EntrySortKeyError] load failed to insert entry type={} count={} entry_id={}", type_id,
                   add_count, entry->entry_id());
    // 条目没进分组: 整次 load 失败, 不再动数量缓存
    return false;
  }
  notify_entry_count_changed(entry, 0, add_count, context);
  FWINSTLOGINFO(logger(), "load success: new type={} count={} entry_id={}", type_id, add_count, entry->entry_id());
  return true;
}

// ============================================================
// 客户端同步的单条处理
//
// 被调用时机: 基类 apply_entries 的两个阶段 (删除阶段每条已找到的条目, 新增/更新阶段每条
// 通过校验的同步数据)。按 entry_id 查找与数据校验都在基类, 这里只改分组与数量缓存。
// ============================================================

ITEM_ALGORITHM_API void ItemNoPositionContainer::on_apply_remove_one(const excel_config_group_ptr_t& /*config_group*/,
                                                                     const item_entry_ptr_t& entry,
                                                                     const ItemOperationContext& context) {
  // 被调用时机: 基类 apply_entries 的删除阶段, 每条已找到的条目一次。
  int64_t old_count = entry->item_instance().item_basic().count();

  remove_entry_from_group(entry);
  set_entry_count(entry, 0);
  notify_entry_count_changed(entry, old_count, 0, context);
}

ITEM_ALGORITHM_API void ItemNoPositionContainer::on_apply_update_one(
    const excel_config_group_ptr_t& /*config_group*/, const item_entry_ptr_t& existing,
    const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update, const ItemOperationContext& context) {
  // 被调用时机: 基类 apply_entries 的新增/更新阶段, 每条通过校验并按 entry_id 查过一次之后。
  const auto& item_basic = update.instance().item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t new_count = item_basic.count();

  if (existing) {
    // 更新已有条目: 本模式位置语义为空, 只需覆盖数据并刷新数量缓存
    int64_t old_count = existing->item_instance().item_basic().count();
    set_entry_instance(existing, update.instance());
    notify_entry_count_changed(existing, old_count, new_count, context);
    return;
  }

  // 新增条目: 沿用同步包里的 entry_id
  item_entry_ptr_t new_entry = make_entry(PROJECT_NAMESPACE_ID::DItemInstance(update.instance()), update.entry_id());
  if (!insert_entry_into_group(new_entry)) {
    FWINSTLOGERROR(logger(), "[EntrySortKeyError] apply add failed to insert entry type={} count={} entry_id={}",
                   type_id, new_count, new_entry->entry_id());
    // 条目没进分组, 不能再动数量缓存, 否则缓存与实时计数会对不上
    return;
  }

  notify_entry_count_changed(new_entry, 0, new_count, context);
}

// ============================================================
// 寻位 (本模式没有格子可找)
//
// 本模式只校验"是不是不占格道具", 校验通过就把声明的位置字段与 container_guid 写回,
// 调用方拿到的结果可以直接喂给 check_add。没有可推算的空位, 因此不支持 ignore_item。
// ============================================================

ITEM_ALGORITHM_API bool ItemNoPositionContainer::on_find_positions(
    const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
    const item_basic_readable_iterable& ignore_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const {
  // 被调用时机: 基类 find_positions_for_instances (及由它实现的 find_positions_for_basics) 在
  // 初始化检查与输出预留之后调用一次。
  if (!ignore_item.empty()) {
    FWINSTLOGWARNING(logger(), "find_positions failed: ignore_item is not supported in no position mode, count={}",
                     ignore_item.size());
    return false;
  }

  failed_item.Clear();

  // 回调返回 false 中断遍历并让本次调用整体失败 (结果写到成功列表里的部分由调用方自己丢弃)
  return items.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& item) -> bool {
    const auto& basic = item.item_basic();

    if (ItemAlgorithmTypeOption::GetItemType(basic.type_id()) == nullptr || !is_item_valid(config_group, basic)) {
      FWINSTLOGWARNING(logger(), "find_positions failed: invalid item type={} count={} guid={}", basic.type_id(),
                       basic.count(), basic.guid());
      return false;
    }

    // 本模式不落位, 只写回声明的位置字段与容器归属 (字段映射由接入层的 apply_position 决定)
    auto* out = success_item.Add();
    *out = item;
    auto* out_basic = out->mutable_item_basic();
    apply_position(*out_basic->mutable_position()->mutable_grid_position(), ItemGridPosition{});
    out_basic->mutable_position()->set_container_guid(get_container_guid());
    return true;
  });
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
