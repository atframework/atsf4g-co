// Copyright 2026 atframework

#include <ItemAlgorithm/Container/ItemInfiniteGridContainer.h>

#include <config/excel/item_type_config.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 无限格子模式的操作实现
//
// 与有限格子的差别:
//   * 不限行列, 不做边界判断;
//   * 每件道具只占一个槽位 (忽略配置里的 row/col 尺寸), 不需要位图, 不需要批次预留;
//   * 位置只是槽位标识, 同一个槽位最多一个条目。
// 因此本文件的流程里没有范围检查、矩形占用推演, 只按锚点位置判定槽位是否被占。
// ============================================================

// ============================================================
// check_add 的批处理钩子与 add 的单条钩子
//
// 批次流程在基类 (持有入参视图 / 初始化检查 / 通用校验 / GUID 唯一 / 类型数量上限 / 循环),
// 这里只补本模式的槽位与堆叠规则:
//   * 位置配置与堆叠上限 (accumulation_limit <= 0 视为无限堆叠);
//   * 目标槽位已被占用时, 只有同类且都无 GUID 才能堆叠, 堆叠后不能超过上限;
//   * 目标槽位在本批次内已被前面的请求预定 / 新建时, 按同样两条规则推演。
// 同批次的占位状态由 on_check_add 在本地创建的 InfiniteGridCheckScratch 维护。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult
ItemInfiniteGridContainer::on_check_add(const ItemAddCheckedRequest& checked_request) const {
  InfiniteGridCheckScratch scratch;
  const auto& config_group = checked_request.config_group;

  // 回调返回 false 中断遍历; 失败结果在中断前写进 result
  ItemOperationResult result;
  size_t index = 0;
  checked_request.requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& request) -> bool {
    size_t i = index++;
    const auto& item_basic = request.item_basic();
    int32_t type_id = item_basic.type_id();
    int64_t add_count = item_basic.count();
    int64_t guid = item_basic.guid();

    auto position_cfg = get_item_position_cfg(config_group, item_basic);
    if (position_cfg == nullptr) {
      int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      FWINSTLOGWARNING(logger(), "check_add failed[{}]: item position cfg not found type={}, error={} ({})", i, type_id,
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      result = {error_code, type_id};
      return false;
    }

    int64_t accumulation_limit = position_cfg->accumulation_limit();
    if (accumulation_limit <= 0) {
      // <=0 视为无限堆叠
      accumulation_limit = INT32_MAX;
    }

    if (add_count > accumulation_limit) {
      int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
      FWINSTLOGWARNING(logger(), "check_add failed[{}]: count={} exceeds accumulation_limit={} type={}, error={} ({})",
                       i, add_count, accumulation_limit, type_id, error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      result = {error_code, type_id};
      return false;
    }

    ItemGridPosition target_pos = extract_position(item_basic.position().grid_position());
    item_entry_ptr_t existing_entry = find_entry_at_position(target_pos);
    if (existing_entry) {
      // 槽位已有条目: 只有同类且都无 GUID 才允许堆叠
      const auto& existing_basic = existing_entry->item_instance().item_basic();
      if (guid != 0 || existing_basic.guid() != 0 || existing_basic.type_id() != type_id) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
        FWINSTLOGWARNING(logger(), "check_add failed[{}]: slot ({},{}) occupied, error={} ({})", i, target_pos.x,
                         target_pos.y, error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }

      int64_t total = existing_basic.count() + scratch.pending_existing_extra[target_pos] + add_count;
      if (total > accumulation_limit) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
        FWINSTLOGWARNING(logger(), "check_add failed[{}]: stack overflow at ({},{}), total={} limit={}, error={} ({})",
                         i, target_pos.x, target_pos.y, total, accumulation_limit, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }
      scratch.pending_existing_extra[target_pos] += add_count;
    } else {
      auto pending_it = scratch.pending_new_slots.find(target_pos);
      if (pending_it != scratch.pending_new_slots.end()) {
        if (guid != 0 || pending_it->second.type_id != type_id) {
          int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
          FWINSTLOGWARNING(logger(), "check_add failed[{}]: slot ({},{}) pending occupied, error={} ({})", i,
                           target_pos.x, target_pos.y, error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
          result = {error_code, type_id};
          return false;
        }
        int64_t total = pending_it->second.accumulated_count + add_count;
        if (total > accumulation_limit) {
          int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
          FWINSTLOGWARNING(logger(),
                           "check_add failed[{}]: pending stack overflow at ({},{}), total={} limit={}, error={} ({})",
                           i, target_pos.x, target_pos.y, total, accumulation_limit, error_code,
                           PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
          result = {error_code, type_id};
          return false;
        }
        pending_it->second.accumulated_count = total;
      } else {
        scratch.pending_new_slots[target_pos] = InfiniteGridCheckScratch::PendingNewSlot{type_id, add_count};
      }
    }

    return true;
  });

  return result;
}

ITEM_ALGORITHM_API ItemOperationResult ItemInfiniteGridContainer::on_add_one(
    ItemAddCheckedRequest& checked_request, const PROJECT_NAMESPACE_ID::DItemInstance& req,
    const ItemOperationContext& context) {
  const auto& item_basic = req.item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t add_count = item_basic.count();
  int64_t guid = item_basic.guid();

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr) {
    FWINSTLOGERROR(logger(), "add failed: unknown item type={} guid={}", type_id, guid);
    return {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND, type_id};
  }

  if (!item_type_config->need_occupy_the_grid) {
    // 无限格子容器只接受占格道具
    FWINSTLOGERROR(logger(), "add failed: ungrid item is not allowed in infinite grid, type={} guid={}", type_id, guid);
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, type_id};
  }

  ItemGridPosition target_pos = extract_position(item_basic.position().grid_position());

  // 尝试堆叠到已有条目 (无 GUID 才可堆叠, check_add 已保证)
  item_entry_ptr_t existing = find_entry_at_position(target_pos);
  if (existing) {
    int64_t old_count = existing->item_instance().item_basic().count();
    set_entry_count(existing, old_count + add_count);
    notify_entry_count_changed(existing, old_count, old_count + add_count, context);
    FWINSTLOGDEBUG(logger(), "add stack type={} count={} at slot ({},{}) entry_id={} total={}", type_id, add_count,
                   target_pos.x, target_pos.y, existing->entry_id(), old_count + add_count);
    return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
  }

  // 新建条目
  auto position_cfg = get_item_position_cfg(checked_request.config_group, item_basic);
  if (position_cfg == nullptr) {
    FWINSTLOGERROR(logger(), "add failed: position cfg not found type={} guid={}", type_id, guid);
    return {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND, type_id};
  }

  // 请求是只读视图, 先拷一份, 把规范化后的槽位写进这份副本再落位
  // (写哪个 proto 字段由接入层的 apply_position 决定)
  PROJECT_NAMESPACE_ID::DItemInstance new_instance = req;
  apply_position(*new_instance.mutable_item_basic()->mutable_position()->mutable_grid_position(), target_pos);

  item_entry_ptr_t entry = make_entry(std::move(new_instance));
  if (!insert_entry_into_group(entry)) {
    FWINSTLOGERROR(logger(),
                   "[EntrySortKeyError] add failed to insert entry type={} count={} guid={} at slot ({},{}) "
                   "entry_id={}",
                   type_id, add_count, guid, target_pos.x, target_pos.y, entry->entry_id());
    // 条目没进分组, 不能再动数量缓存, 否则缓存与实时计数会对不上
    return {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED, type_id};
  }
  add_entry_index(*position_cfg, entry);
  notify_entry_count_changed(entry, 0, add_count, context);
  FWINSTLOGDEBUG(logger(), "add new type={} count={} guid={} at slot ({},{}) entry_id={}", type_id, add_count,
                 entry->item_instance().item_basic().guid(), target_pos.x, target_pos.y, entry->entry_id());
  return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
}

// ============================================================
// check_sub 的批处理钩子与 sub 的单条钩子
//
// 扣减只有两种定位方式:
//   * 带 GUID 的道具按 GUID 定位 (数量固定为 1)
//   * 其余按槽位定位
// 基类已做完通用校验与"按类型总数够扣"判断, 这里按上述两种定位方式补本模式的校验,
// 并用本地状态累计同批次内同一 GUID / 槽位已被要求扣减的数量。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult
ItemInfiniteGridContainer::on_check_sub(const ItemSubCheckedRequest& checked_request) const {
  InfiniteGridCheckScratch scratch;
  const auto& config_group = checked_request.config_group;

  // 回调返回 false 中断遍历; 失败结果在中断前写进 result
  ItemOperationResult result;
  size_t index = 0;
  checked_request.requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    size_t i = index++;
    int64_t guid = request.guid();
    int32_t type_id = request.type_id();
    int64_t sub_count = request.count();

    auto position_cfg = get_item_position_cfg(config_group, request);
    if (position_cfg == nullptr) {
      int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      FWINSTLOGWARNING(logger(), "check_sub failed[{}]: item position cfg not found type={}, error={} ({})", i, type_id,
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      result = {error_code, type_id};
      return false;
    }

    if (guid != 0) {
      // 带GUID 跳过位置直接索引
      if (sub_count != 1) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
        FWINSTLOGWARNING(logger(), "check_sub failed[{}]: guid item must sub count=1, got {}, error={} ({})", i,
                         sub_count, error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }

      item_entry_ptr_t guid_entry = find_entry_by_guid(guid);
      if (!guid_entry) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        FWINSTLOGWARNING(logger(), "check_sub failed[{}]: guid={} not found, error={} ({})", i, guid, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }

      // 检查数量与类型
      if (guid_entry->item_instance().item_basic().type_id() != type_id ||
          guid_entry->item_instance().item_basic().count() < sub_count) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        FWINSTLOGWARNING(logger(), "check_sub failed[{}]: guid={} type/count mismatch, error={} ({})", i, guid,
                         error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }

      if (!scratch.pending_sub_guids.insert(guid).second) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID;
        FWINSTLOGWARNING(logger(), "check_sub failed[{}]: duplicate guid={}, error={} ({})", i, guid, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }
    } else {
      // 按槽位预扣减, 跟踪同一槽位多次扣减
      ItemGridPosition target_pos = extract_position(request.position().grid_position());
      item_entry_ptr_t position_entry = find_entry_at_position(target_pos);
      if (!position_entry) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        FWINSTLOGWARNING(logger(), "check_sub failed[{}]: slot ({},{}) empty, error={} ({})", i, target_pos.x,
                         target_pos.y, error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }

      const auto& entry_basic = position_entry->item_instance().item_basic();
      if (entry_basic.type_id() != type_id) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
        FWINSTLOGWARNING(logger(), "check_sub failed[{}]: slot ({},{}) type mismatch, error={} ({})", i, target_pos.x,
                         target_pos.y, error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }

      int64_t current = entry_basic.count();
      int64_t already_sub = scratch.pending_sub_counts[target_pos];
      if (current - already_sub < sub_count) {
        int32_t error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
        FWINSTLOGWARNING(logger(),
                         "check_sub failed[{}]: not enough at ({},{}), current={} already_sub={} sub={}, error= ({})",
                         i, target_pos.x, target_pos.y, current, already_sub, sub_count, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        result = {error_code, type_id};
        return false;
      }
      scratch.pending_sub_counts[target_pos] = already_sub + sub_count;
    }

    return true;
  });

  return result;
}

ITEM_ALGORITHM_API ItemOperationResult ItemInfiniteGridContainer::on_sub_one(
    ItemSubCheckedRequest& checked_request, const PROJECT_NAMESPACE_ID::DItemBasic& req,
    const ItemOperationContext& context) {
  int64_t sub_count = req.count();
  int32_t type_id = req.type_id();

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
    // check_sub 已保证类型存在且占格, 这里兜底避免配置被改动后走到不该走的分支
    FWINSTLOGERROR(logger(), "sub failed: invalid item type={}, skip", type_id);
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, type_id};
  }

  item_entry_ptr_t entry = find_entry(req);
  if (!entry) {
    // check_sub 通过后条目仍可能已不在 (例如此前被 clear / 同步改过), 这里做空指针兜底
    FWINSTLOGERROR(logger(), "sub failed: entry not found type={} guid={}, skip", type_id, req.guid());
    return {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND, type_id};
  }

  int64_t current_count = entry->item_instance().item_basic().count();
  int64_t guid = entry->item_instance().item_basic().guid();
  ItemGridPosition entry_pos = extract_position(entry->item_instance().item_basic().position().grid_position());

  auto position_cfg = get_item_position_cfg(checked_request.config_group, req);
  if (position_cfg == nullptr) {
    FWINSTLOGERROR(logger(), "sub failed: position cfg not found type={} guid={}, skip", type_id, guid);
    return {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND, type_id};
  }

  if (current_count <= sub_count) {
    // 完全移除: 槽位索引与 GUID 索引随条目一起摘掉
    remove_entry_index(*position_cfg, entry);
    remove_entry_from_group(entry);
    set_entry_count(entry, 0);
    notify_entry_count_changed(entry, current_count, 0, context);
    FWINSTLOGDEBUG(logger(), "sub remove all type={} count={} guid={} entry_id={} at slot ({},{})", type_id,
                   current_count, guid, entry->entry_id(), entry_pos.x, entry_pos.y);
  } else {
    // 部分扣减
    set_entry_count(entry, current_count - sub_count);
    notify_entry_count_changed(entry, current_count, current_count - sub_count, context);
    FWINSTLOGDEBUG(logger(), "sub partial type={} count={} guid={} entry_id={} remaining={}", type_id, sub_count, guid,
                   entry->entry_id(), current_count - sub_count);
  }

  return {PROJECT_NAMESPACE_ID::EN_SUCCESS, 0};
}

// ============================================================
// check_has 的批处理钩子
//
// 带 GUID 的道具按 GUID 对账, 其余道具按槽位对账。
// 基类只按类型总量判断够不够, 覆盖不到"指定 GUID / 槽位上的条目是否存在、类型是否一致、
// 数量是否够", 所以这里按定位方式逐条补; 同批次内同一 GUID / 槽位的多次需求用本地状态累计。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult ItemInfiniteGridContainer::on_check_has(
    const excel_config_group_ptr_t& /*config_group*/, const item_basic_readable_iterable& requests) const {
  InfiniteGridCheckScratch scratch;
  ItemOperationResult result;

  requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    int32_t type_id = request.type_id();
    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config != nullptr && item_type_config->need_guid) {
      item_entry_ptr_t guid_entry = find_entry_by_guid(request.guid());
      if (!guid_entry || guid_entry->item_instance().item_basic().type_id() != type_id) {
        result = {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND, type_id};
        return false;
      }

      int64_t required_count = scratch.pending_has_guids[request.guid()] + request.count();
      if (guid_entry->item_instance().item_basic().count() < required_count) {
        result = {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH, type_id};
        return false;
      }
      scratch.pending_has_guids[request.guid()] = required_count;
    } else {
      ItemGridPosition position = extract_position(request.position().grid_position());
      item_entry_ptr_t position_entry = find_entry_at_position(position);
      if (!position_entry || position_entry->item_instance().item_basic().type_id() != type_id) {
        result = {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND, type_id};
        return false;
      }

      int64_t required_count = scratch.pending_has_positions[position] + request.count();
      if (position_entry->item_instance().item_basic().count() < required_count) {
        result = {PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH, type_id};
        return false;
      }
      scratch.pending_has_positions[position] = required_count;
    }

    return true;
  });

  return result;
}

// ============================================================
// move / check_move
//
// 槽位腾挪: 先扣掉来源条目 (整体移除时释放槽位), 再把道具放到目标槽位
// (目标已有同类无 GUID 条目时合入)。
// ============================================================

ITEM_ALGORITHM_API ItemOperationResult ItemInfiniteGridContainer::on_move_sub_one(
    ItemMoveCheckedRequest& checked_request, const ItemMoveSubRequest& request, const ItemOperationContext& context) {
  // 被调用时机: 基类 move 的 kMoveSub 阶段逐条循环, 每条一次。
  int64_t source_count = request.entry->item_instance().item_basic().count();
  int64_t guid = request.entry->item_instance().item_basic().guid();
  int32_t type_id = request.entry->item_instance().item_basic().type_id();
  bool sub_is_whole = (source_count == request.op_count);
  auto position_cfg = get_item_position_cfg(checked_request.config_group, request.entry->item_instance().item_basic());

  if (sub_is_whole) {
    // 整体移除索引 (含槽位与 GUID)
    if (position_cfg != nullptr) {
      remove_entry_index(*position_cfg, request.entry);
    }
    remove_entry_from_group(request.entry);
    set_entry_count(request.entry, 0);
    int64_t total_count = change_cached_item_count(type_id, -source_count);
    on_item_count_changed(type_id, request.entry, guid, request.position, source_count, 0, total_count, context);
    on_item_data_changed(request.entry, context);
    FWINSTLOGDEBUG(logger(), "move sub all type={} count={} guid={} entry_id={} from slot ({},{})", type_id,
                   source_count, guid, request.entry->entry_id(), request.position.x, request.position.y);
  } else {
    // 部分扣减
    set_entry_count(request.entry, source_count - request.op_count);
    int64_t total_count = change_cached_item_count(type_id, -request.op_count);
    on_item_count_changed(type_id, request.entry, guid, request.position, source_count, source_count - request.op_count,
                          total_count, context);
    on_item_data_changed(request.entry, context);
    FWINSTLOGDEBUG(logger(), "move sub partial type={} count={} guid={} entry_id={} remaining={}", type_id,
                   request.op_count, guid, request.entry->entry_id(), source_count - request.op_count);
  }

  return {};
}

ITEM_ALGORITHM_API ItemOperationResult ItemInfiniteGridContainer::on_move_add_one(
    ItemMoveCheckedRequest& checked_request, const ItemMoveAddRequest& request, const ItemOperationContext& context) {
  // 被调用时机: 基类 move 的 kMoveAdd 阶段逐条循环, 每条一次 (全部 kMoveSub 之后)。
  int32_t type_id = request.type_id;

  // 检查目标槽位是否有已有条目 (用于堆叠合入)
  item_entry_ptr_t target_entry = find_entry_at_position(request.position);
  if (target_entry) {
    // 合入已有条目 (堆叠)
    int64_t target_old = target_entry->item_instance().item_basic().count();
    set_entry_count(target_entry, target_old + request.op_count);
    int64_t total_count = change_cached_item_count(type_id, request.op_count);
    on_item_count_changed(type_id, target_entry, target_entry->item_instance().item_basic().guid(), request.position,
                          target_old, target_old + request.op_count, total_count, context);
    on_item_data_changed(target_entry, context);
    FWINSTLOGDEBUG(logger(), "move add merge type={} count={} at slot ({},{}) entry_id={} total={}", type_id,
                   request.op_count, request.position.x, request.position.y, target_entry->entry_id(),
                   target_old + request.op_count);
    return {};
  }

  // 新建条目 (移入): 从 add_entry 复制数据。
  // 位置配置只有这条路径需要, 放到这里查, 保证查询失败时还没动任何索引
  auto position_cfg = get_item_position_cfg(checked_request.config_group, request.entry->item_instance().item_basic());
  if (position_cfg == nullptr) {
    FWINSTLOGERROR(logger(), "move add failed: position cfg not found type={} guid={}", type_id,
                   request.entry->item_instance().item_basic().guid());
    return {};
  }

  PROJECT_NAMESPACE_ID::DItemInstance new_instance = request.entry->item_instance();
  new_instance.mutable_item_basic()->set_count(request.op_count);
  *new_instance.mutable_item_basic()->mutable_position() = request.goal_position;
  item_entry_ptr_t new_entry = make_entry(std::move(new_instance));
  if (!insert_entry_into_group(new_entry)) {
    FWINSTLOGERROR(logger(),
                   "[EntrySortKeyError] add failed to insert entry type={} count={} guid={} at slot ({},{}) "
                   "entry_id={}",
                   type_id, request.op_count, new_entry->item_instance().item_basic().guid(), request.position.x,
                   request.position.y, new_entry->entry_id());
    // 条目没进分组, 不能再动数量缓存, 否则缓存与实时计数会对不上
    return {};
  }
  add_entry_index(*position_cfg, new_entry);
  int64_t total_count = change_cached_item_count(type_id, request.op_count);
  on_item_count_changed(type_id, new_entry, new_entry->item_instance().item_basic().guid(), request.position, 0,
                        request.op_count, total_count, context);
  on_item_data_changed(new_entry, context);
  FWINSTLOGDEBUG(logger(), "move add new type={} count={} guid={} at slot ({},{}) entry_id={}", type_id,
                 request.op_count, new_entry->item_instance().item_basic().guid(), request.position.x,
                 request.position.y, new_entry->entry_id());
  return {};
}

bool ItemInfiniteGridContainer::check_move_request(const excel_config_group_ptr_t& config_group,
                                                   ItemMoveCheckedRequest& checked_request) const {
  auto& error_code = checked_request.result.error_code;

  // ============================================================
  // 1. 检查 Sub Entry 是否重复, op_count 合法, 填充 Helper 字段
  // ============================================================
  std::unordered_set<const ItemEntry*> sub_entry_set;
  std::unordered_map<int32_t, int64_t> type_count_delta;

  for (auto& sub_req : checked_request.request.move_sub_entrys) {
    if (!sub_req.entry || sub_req.op_count <= 0) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGWARNING(logger(), "check_move failed: invalid move_sub entry/op_count, error={} ({})", error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }
    if (!sub_entry_set.insert(sub_req.entry.get()).second) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // entry 重复
      FWINSTLOGWARNING(logger(), "check_move failed: duplicate move_sub entry_id={}, error={} ({})",
                       sub_req.entry->entry_id(), error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    if (sub_req.entry->item_instance().item_basic().position().container_guid() != get_container_guid()) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGWARNING(logger(),
                       "check_move failed: source container guid mismatch, expected={} actual={} entry_id={}, error={} "
                       "({})",
                       get_container_guid(), sub_req.entry->item_instance().item_basic().position().container_guid(),
                       sub_req.entry->entry_id(), error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    if (!check_item_position(sub_req.entry->item_instance().item_basic().position())) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGERROR(logger(), "check_move failed: invalid item position entry_id={}, error={} ({})",
                     sub_req.entry->entry_id(), error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    const auto& ref_basic = sub_req.entry->item_instance().item_basic();
    int32_t type_id = ref_basic.type_id();

    // op_count 不能超过 entry 的 count
    if (sub_req.op_count > ref_basic.count()) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
      checked_request.result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(),
                       "check_move failed: move_sub count={} exceeds entry count={} entry_id={}, error={} ({})",
                       sub_req.op_count, ref_basic.count(), sub_req.entry->entry_id(), error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGWARNING(logger(), "check_move failed: move_sub type={} must be grid item, error={} ({})", type_id,
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    if (get_item_position_cfg(config_group, ref_basic) == nullptr) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      FWINSTLOGWARNING(logger(), "check_move failed: move_sub position cfg not found type={}, error={} ({})", type_id,
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    // 填充 Helper 字段 (本模式每件占一个槽位)
    sub_req.position = extract_position(ref_basic.position().grid_position());
    sub_req.item_row = 1;
    sub_req.item_col = 1;
    type_count_delta[type_id] -= sub_req.op_count;
  }

  // ============================================================
  // 2. 检查 Add Entry 重复性, op_count, 填充 Helper, accumulation_limit, GUID
  // ============================================================
  std::unordered_set<const ItemEntry*> add_entry_set;
  std::unordered_set<int64_t> pending_guids;

  for (auto& add_req : checked_request.request.move_add_entrys) {
    if (!add_req.entry || add_req.op_count <= 0) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGWARNING(logger(), "check_move failed: invalid move_add entry/op_count, error={} ({})", error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }
    if (!add_entry_set.insert(add_req.entry.get()).second) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;  // entry 重复
      FWINSTLOGWARNING(logger(), "check_move failed: duplicate move_add entry_id={}, error={} ({})",
                       add_req.entry->entry_id(), error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    if (add_req.goal_position.container_guid() != get_container_guid()) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGWARNING(logger(),
                       "check_move failed: container guid mismatch, expected={} actual={} entry_id={}, error={} ({})",
                       get_container_guid(), add_req.goal_position.container_guid(), add_req.entry->entry_id(),
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    if (!check_item_position(add_req.goal_position)) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGERROR(logger(), "check_move failed: invalid item position entry_id={}, error={} ({})",
                     add_req.entry->entry_id(), error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    const auto& ref_basic = add_req.entry->item_instance().item_basic();
    int32_t type_id = ref_basic.type_id();

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
    if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      FWINSTLOGWARNING(logger(), "check_move failed: move_add type={} must be grid item, error={} ({})", type_id,
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    auto position_cfg = get_item_position_cfg(config_group, ref_basic);
    if (position_cfg == nullptr) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      FWINSTLOGWARNING(logger(), "check_move failed: move_add position cfg not found type={}, error={} ({})", type_id,
                       error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    int64_t accumulation_limit = position_cfg->accumulation_limit();
    if (accumulation_limit <= 0) {
      // <=0 视为无限堆叠
      accumulation_limit = INT32_MAX;
    }

    // 填充 Helper 字段 (本模式每件占一个槽位)
    add_req.type_id = type_id;
    add_req.position = extract_position(add_req.goal_position.grid_position());
    add_req.item_row = 1;
    add_req.item_col = 1;
    add_req.accumulation_limit = accumulation_limit;

    // 检查 op_count 不超过 accumulation_limit
    if (add_req.op_count > accumulation_limit) {
      error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
      FWINSTLOGWARNING(logger(), "check_move failed: move_add count={} exceeds limit={} type={}, error={} ({})",
                       add_req.op_count, accumulation_limit, type_id, error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
      return false;
    }

    bool is_paired = (sub_entry_set.count(add_req.entry.get()) > 0);
    if (!is_paired) {
      // Add-only (从外部移入): 检查 GUID 唯一性
      int64_t guid = ref_basic.guid();
      if (guid != 0) {
        if (has_entry_guid(guid) || !pending_guids.insert(guid).second) {
          error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID;
          FWINSTLOGWARNING(logger(), "check_move failed: duplicate guid={} on move_add, error={} ({})", guid,
                           error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
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
      int64_t current_total = get_item_count(delta_pair.first);
      int32_t limit_ret = on_check_item_count_limit(delta_pair.first, current_total, delta_pair.second);
      if (limit_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
        error_code = limit_ret;
        FWINSTLOGWARNING(logger(), "check_move failed: count limit exceeded type={} current={} add={}, error={} ({})",
                         delta_pair.first, current_total, delta_pair.second, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        return false;
      }
    }
  }

  return true;
}

ITEM_ALGORITHM_API bool ItemInfiniteGridContainer::on_check_move(ItemMoveCheckedRequest& checked_request) const {
  // 被调用时机: 基类 check_move 完成初始化检查与空请求判断后调用一次 (本模式的移动语义都在这里)。
  auto& error_code = checked_request.result.error_code;

  // ============================================================
  // Phase 0: 验证入参
  // ============================================================
  if (!check_move_request(checked_request.config_group, checked_request)) {
    return false;
  }

  // ============================================================
  // Phase 1 (Sub): 记录会被整体移走的来源槽位
  // ============================================================
  position_set_type removed_anchors;

  for (const auto& op : checked_request.request.move_sub_entrys) {
    if (op.entry->item_instance().item_basic().count() > op.op_count) {
      continue;
    }
    removed_anchors.insert(op.position);
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

  for (const auto& op : checked_request.request.move_add_entrys) {
    // ---- 1. 检查目标槽位是否有未被移走的已有条目 (用于合入) ----
    item_entry_ptr_t existing_entry = find_entry_at_position(op.position);
    bool has_unmoved_existing = (existing_entry != nullptr && removed_anchors.count(op.position) == 0);

    if (has_unmoved_existing) {
      // 目标槽位有未被移走的已有条目, 只能合入
      const auto& target_basic = existing_entry->item_instance().item_basic();

      if (target_basic.guid() != 0 || target_basic.type_id() != op.type_id) {
        // 目标槽位有不同类型或带GUID的条目, 无法合入
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_MOVE_TARGET_OCCUPIED;
        FWINSTLOGWARNING(logger(),
                         "check_move failed: move target slot ({},{}) occupied by type={} guid={}, error={} ({})",
                         op.position.x, op.position.y, target_basic.type_id(), target_basic.guid(), error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        return false;
      }

      int64_t target_total = target_basic.count() + pending_merge_extra[op.position] + op.op_count;
      if (target_total > op.accumulation_limit) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
        FWINSTLOGWARNING(logger(),
                         "check_move failed: merge stack overflow at slot ({},{}), total={} limit={}, error={} ({})",
                         op.position.x, op.position.y, target_total, op.accumulation_limit, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        return false;
      }
      pending_merge_extra[op.position] += op.op_count;
      continue;
    }

    // ---- 2. 检查本批次是否已在此槽位创建了新条目 ----
    auto pending_it = pending_new_anchors.find(op.position);
    if (pending_it != pending_new_anchors.end()) {
      if (pending_it->second.has_guid || pending_it->second.type_id != op.type_id) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_POSITION_OCCUPIED;
        FWINSTLOGWARNING(logger(), "check_move failed: pending slot ({},{}) occupied, error={} ({})", op.position.x,
                         op.position.y, error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        return false;
      }
      int64_t total = pending_it->second.count + op.op_count;
      if (total > pending_it->second.accumulation_limit) {
        error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_STACK_OVERFLOW;
        FWINSTLOGWARNING(logger(),
                         "check_move failed: pending stack overflow at slot ({},{}), total={} limit={}, error={} ({})",
                         op.position.x, op.position.y, total, pending_it->second.accumulation_limit, error_code,
                         PROJECT_NAMESPACE_ID::EnErrorCode_Name(error_code));
        return false;
      }
      pending_it->second.count = total;
      continue;
    }

    // ---- 3. 全新槽位: 本模式不做范围与矩形占用判断, 上面已确认槽位上没有条目 ----
    pending_new_anchors[op.position] =
        PendingNewAnchor{op.type_id, static_cast<int64_t>(op.op_count),
                         op.entry->item_instance().item_basic().guid() != 0, op.accumulation_limit};
  }

  return true;
}

// ============================================================
// load 的单条钩子
//
// 初始化后按条目载入: 同槽位同类无 GUID 条目合并, 否则新建。
// 基类已做完通用校验 (条目字段 / 容器归属 / 位置字段 / 类型存在 / GUID 唯一 / 数量上限),
// 这里只做落位。本模式不做边界检查, 槽位是否空闲由上面的合并分支判定;
// 这里不检查堆叠上限 (载入的是已经存在的存档数据)。
// ============================================================

ITEM_ALGORITHM_API bool ItemInfiniteGridContainer::on_load_one(const excel_config_group_ptr_t& config_group,
                                                               const PROJECT_NAMESPACE_ID::DItemInstance& item_instance,
                                                               const ItemOperationContext& context) {
  const auto& item_basic = item_instance.item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t add_count = item_basic.count();
  int64_t guid = item_basic.guid();

  // 不占格道具在本模式一律不合法 (基类的 is_item_valid 已挡了一层, 这里兜底)
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
    FWINSTLOGWARNING(logger(), "load failed: ungrid item is not allowed in infinite grid, type={}", type_id);
    return false;
  }

  auto position_cfg = get_item_position_cfg(config_group, item_basic);
  if (position_cfg == nullptr) {
    FWINSTLOGWARNING(logger(), "load failed: item position cfg not found type={}", type_id);
    return false;
  }

  ItemGridPosition target_pos = extract_position(item_basic.position().grid_position());

  // 堆叠到已有条目
  item_entry_ptr_t existing = find_entry_at_position(target_pos);
  if (existing) {
    const auto& existing_basic = existing->item_instance().item_basic();
    if (guid != 0 || existing_basic.guid() != 0 || existing_basic.type_id() != type_id) {
      FWINSTLOGWARNING(logger(), "load failed: slot ({},{}) occupied, error conflict type={} guid={}", target_pos.x,
                       target_pos.y, type_id, guid);
      return false;
    }
    int64_t old_count = existing_basic.count();
    set_entry_count(existing, old_count + add_count);
    notify_entry_count_changed(existing, old_count, old_count + add_count, context);
    FWINSTLOGINFO(logger(), "load success: stack type={} count={} at slot ({},{}) entry_id={} total={}", type_id,
                  add_count, target_pos.x, target_pos.y, existing->entry_id(), old_count + add_count);
    return true;
  }

  // 放入 (槽位路径上没有条目, 无需再判占用)
  PROJECT_NAMESPACE_ID::DItemInstance new_instance = item_instance;
  apply_position(*new_instance.mutable_item_basic()->mutable_position()->mutable_grid_position(), target_pos);

  item_entry_ptr_t entry = make_entry(std::move(new_instance));
  if (!insert_entry_into_group(entry)) {
    FWINSTLOGERROR(logger(),
                   "[EntrySortKeyError] load failed to insert entry type={} count={} guid={} at slot ({},{}) "
                   "entry_id={}",
                   type_id, add_count, guid, target_pos.x, target_pos.y, entry->entry_id());
    // 条目没进分组: 整次 load 失败, 不再动数量缓存
    return false;
  }
  add_entry_index(*position_cfg, entry);
  notify_entry_count_changed(entry, 0, add_count, context);
  FWINSTLOGINFO(logger(), "load success: new type={} count={} guid={} at slot ({},{}) entry_id={}", type_id, add_count,
                entry->item_instance().item_basic().guid(), target_pos.x, target_pos.y, entry->entry_id());
  return true;
}

// ============================================================
// apply_entries 的单条钩子
//
// 客户端同步: 按 entry_id 删除 / 新增或更新条目。
// 基类负责流程 (遍历 remove_entry_ids 找条目后逐条调删除钩子; 遍历 update_entries, 做通用校验、
// 按 entry_id 找已有条目后逐条调新增/更新钩子)。
// 这里只管本模式的数据结构: 槽位 / GUID 索引的挂摘、进出排序分组、数量缓存与通知。
// ============================================================

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::on_apply_remove_one(const excel_config_group_ptr_t& /*config_group*/,
                                                                       const item_entry_ptr_t& entry,
                                                                       const ItemOperationContext& context) {
  if (!entry) {
    return;
  }

  int32_t type_id = entry->item_instance().item_basic().type_id();
  int64_t old_count = entry->item_instance().item_basic().count();
  int64_t guid = entry->item_instance().item_basic().guid();

  // 槽位索引 / GUID 索引都随条目一起摘掉
  detach_entry_index(entry);
  remove_entry_from_group(entry);
  set_entry_count(entry, 0);
  notify_entry_count_changed(entry, old_count, 0, context);
  FWINSTLOGINFO(logger(), "apply remove entry_id={} type={} count={} guid={}", entry->entry_id(), type_id, old_count,
                guid);
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::on_apply_update_one(
    const excel_config_group_ptr_t& /*config_group*/, const item_entry_ptr_t& existing,
    const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update, const ItemOperationContext& context) {
  const auto& item_basic = update.instance().item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t new_count = item_basic.count();
  int64_t guid = item_basic.guid();

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(type_id);
  if (item_type_config == nullptr || !item_type_config->need_occupy_the_grid) {
    FWINSTLOGERROR(logger(), "apply update failed: invalid item type={} entry_id={} guid={}", type_id,
                   update.entry_id(), guid);
    return;
  }

  if (existing) {
    // --- 更新已有 entry ---
    int64_t old_count = existing->item_instance().item_basic().count();
    int64_t old_guid = existing->item_instance().item_basic().guid();
    ItemGridPosition old_pos = extract_position(existing->item_instance().item_basic().position().grid_position());
    ItemGridPosition new_pos = extract_position(item_basic.position().grid_position());

    bool position_changed = (old_pos != new_pos);
    if (position_changed) {
      // 旧槽位与旧 GUID 先摘掉: 覆盖数据后 extract_position 取到的就是新槽位
      detach_entry_index(existing);
    }

    // 覆盖 item_instance
    set_entry_instance(existing, update.instance());

    // 排序键由容器按位置语义算, 槽位变了要重新排序: 先从分组摘出, 刷键后再插回
    if (position_changed) {
      remove_entry_from_group(existing);
      set_entry_sort_key(existing);
      if (!insert_entry_into_group(existing)) {
        FWINSTLOGERROR(logger(), "[EntrySortKeyError] apply update failed to re-insert entry type={} entry_id={}",
                       type_id, existing->entry_id());
      }
      attach_entry_index(existing);
    } else if (old_guid != guid) {
      // 位置没变时索引不会随 attach_entry_index 一起刷新, 这里按新 GUID 重挂
      if (old_guid != 0) {
        detach_entry_guid(old_guid);
      }
      if (guid != 0) {
        attach_entry_guid(guid, existing);
      }
    }

    // 数量缓存与通知 (条目数据已经覆盖完成, 这里只用新旧数量算差值)
    notify_entry_count_changed(existing, old_count, new_count, context);
    FWINSTLOGINFO(logger(), "apply update entry_id={} type={} count={} guid={}", update.entry_id(), type_id, new_count,
                  guid);
  } else {
    // --- 新增 entry ---
    item_entry_ptr_t new_entry = make_entry(PROJECT_NAMESPACE_ID::DItemInstance(update.instance()), update.entry_id());

    if (!insert_entry_into_group(new_entry)) {
      FWINSTLOGERROR(logger(),
                     "[EntrySortKeyError] apply add failed to insert entry type={} count={} guid={} entry_id={}",
                     type_id, new_count, guid, new_entry->entry_id());
      // 条目没进分组, 不能再动数量缓存, 否则缓存与实时计数会对不上
      return;
    }

    attach_entry_index(new_entry);
    notify_entry_count_changed(new_entry, 0, new_count, context);
    FWINSTLOGINFO(logger(), "apply add entry_id={} type={} count={} guid={}", update.entry_id(), type_id, new_count,
                  guid);
  }
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
