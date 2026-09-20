// Copyright 2026 atframework

#include <ItemAlgorithm/ItemContainer.h>

#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _MSC_VER
#  include <intrin.h>
#endif

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

ITEM_ALGORITHM_API ItemAddCheckedRequest ItemContainer::check_add(const excel_config_group_ptr_t& config_group,
                                                                  item_instance_readable_iterable& requests,
                                                                  const ItemOperationSource& source) const {
  // 入参是具名视图 (非 const 引用, 临时视图传不进来), checked request 只引用它, 不复制请求数据
  ItemAddCheckedRequest checked_request{config_group, requests, get_container_guid(), issue_operate_id(), source};
  auto& result = checked_request.result;
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "check_add called before init, container_guid={} operate_id={}", get_container_guid(),
                   get_operate_id());
    result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN;
    return checked_request;
  }

  // 基类自己维护的两份批次累计: 数量上限判断用的类型累计, 与批次内已出现的 GUID
  std::unordered_map<int32_t, int64_t> pending_type_add_count;
  std::unordered_set<int64_t> pending_guids;

  // 回调返回 false 中断遍历; 错误码在中断前写进 result
  size_t index = 0;
  checked_request.requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& request) -> bool {
    size_t i = index++;
    const auto& item_basic = request.item_basic();
    int32_t type_id = item_basic.type_id();
    int64_t add_count = item_basic.count();
    int64_t guid = item_basic.guid();

    // 本模式声明可跳过的请求 (无位置容器的 count == 0): 不校验也不执行
    if (should_skip_add_request(request)) {
      FWINSTLOGDEBUG(logger(), "check_add skip[{}]: empty request type={} count={} guid={}", i, type_id, add_count,
                     guid);
      return true;
    }

    int32_t check_ret = validate_item_basic(checked_request.config_group, item_basic);
    if (check_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = check_ret;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(), "check_add failed[{}]: invalid item type={} count={} guid={}, error={} ({})", i,
                       type_id, add_count, guid, result.error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }

    // GUID 唯一: 既不能与容器内已有条目重复, 也不能与同批次前面的请求重复
    if (guid != 0 && (has_entry_guid(guid) || pending_guids.count(guid) > 0)) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_DUPLICATE_GUID;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(), "check_add failed[{}]: duplicate guid={}, error={} ({})", i, guid, result.error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }

    // 类型总量上限 (含同批次前面的累计)
    int64_t current_total = get_item_count(type_id) + pending_type_add_count[type_id];
    int32_t limit_ret = on_check_item_count_limit(type_id, current_total, add_count);
    if (limit_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = limit_ret;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(),
                       "check_add failed[{}]: item count limit exceeded type={} current={} add={}, error={} ({})", i,
                       type_id, current_total, add_count, result.error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }
    pending_type_add_count[type_id] += add_count;

    // 单条业务校验 (业务容器覆盖 on_check_add_one)
    int32_t one_ret = on_check_add_one(checked_request.config_group, request);
    if (one_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = one_ret;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(
          logger(), "check_add failed[{}]: rejected by on_check_add_one type={} count={} guid={}, error={} ({})", i,
          type_id, add_count, guid, result.error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }

    if (guid != 0) {
      pending_guids.insert(guid);
    }
    FWINSTLOGDEBUG(logger(), "check_add pass[{}]: type={} count={} guid={}", i, type_id, add_count, guid);
    return true;
  });

  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request;
  }

  // 通用字段校验全部通过后, 一次性把整批交给子类检查位置、占位、堆叠等模式规则。
  // 这让子类可以持有整批的临时状态, 不必通过基类的 scratch 反复往返。
  ItemOperationResult mode_result = on_check_add(checked_request);
  if (mode_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    result = mode_result;
    FWINSTLOGWARNING(logger(), "check_add rejected by mode, error={} ({})", result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
    return checked_request;
  }

  FWINSTLOGDEBUG(logger(), "check_add pass, {} requests checked", checked_request.requests.size());
  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::add(ItemAddCheckedRequest& checked_request,
                                                          ItemOperationReason reason) {
  if (!is_operation_allowed("ItemContainer::add")) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }

  ItemOperationResult result = validate_checked_request(
      checked_request.result, checked_request.apply, checked_request.container_guid, checked_request.operate_id, "add");
  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return result;
  }
  checked_request.apply = true;

  // 钩子上下文: 原因用调用方传入的 reason, 来源用 check_add 时存下的 source
  const ItemOperationContext context{reason, checked_request.source};

  size_t index = 0;
  ItemOperationResult one_result;
  checked_request.requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& request) -> bool {
    size_t i = index++;
    // 本模式声明可跳过的请求 (无位置容器的 count == 0): 不落位
    if (should_skip_add_request(request)) {
      FWINSTLOGDEBUG(logger(), "add skip[{}]: empty request type={}", i, request.item_basic().type_id());
      return true;
    }

    one_result = on_add_one(checked_request, request, context);
    if (one_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWINSTLOGERROR(logger(), "add failed[{}]: error={} ({})", i, one_result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(one_result.error_code));
      return false;
    }
    return true;
  });
  if (one_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return one_result;
  }

  FWINSTLOGINFO(logger(), "add success, {} requests applied", checked_request.requests.size());
  return result;
}

ITEM_ALGORITHM_API ItemSubCheckedRequest ItemContainer::check_sub(const excel_config_group_ptr_t& config_group,
                                                                  item_basic_readable_iterable& requests,
                                                                  const ItemOperationSource& source) const {
  // 入参是具名视图 (非 const 引用, 临时视图传不进来), checked request 只引用它, 不复制请求数据
  ItemSubCheckedRequest checked_request{config_group, requests, get_container_guid(), issue_operate_id(), source};
  auto& result = checked_request.result;
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "check_sub called before init, container_guid={} operate_id={}", get_container_guid(),
                   get_operate_id());
    result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN;
    return checked_request;
  }

  // 按类型累计本批次已要求扣减的数量, 避免同一类型的多条请求各自都能通过
  std::unordered_map<int32_t, int64_t> pending_type_sub_count;

  // 回调返回 false 中断遍历; 错误码在中断前写进 result
  size_t index = 0;
  checked_request.requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    size_t i = index++;
    int32_t type_id = request.type_id();
    int64_t sub_count = request.count();
    int64_t guid = request.guid();

    int32_t check_ret = validate_item_basic(checked_request.config_group, request);
    if (check_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = check_ret;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(), "check_sub failed[{}]: invalid item type={} count={} guid={}, error={} ({})", i,
                       type_id, sub_count, guid, result.error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }

    if (sub_count <= 0) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(), "check_sub failed[{}]: sub count={} must be positive, error={} ({})", i, sub_count,
                       result.error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }

    // 按类型总数对账 (实时求和, 不用缓存, 避免缓存偏差放过不该放的请求)
    int64_t total_count = count_type_total(type_id);
    int64_t already_sub = pending_type_sub_count[type_id];
    if (total_count - already_sub < sub_count) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(logger(),
                       "check_sub failed[{}]: not enough type={}, total={} already_sub={} sub={}, error={} ({})", i,
                       type_id, total_count, already_sub, sub_count, result.error_code,
                       PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }
    pending_type_sub_count[type_id] = already_sub + sub_count;

    // 单条业务校验 (业务容器覆盖 on_check_sub_one)
    int32_t one_ret = on_check_sub_one(checked_request.config_group, request);
    if (one_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = one_ret;
      result.failed_type_id = type_id;
      FWINSTLOGWARNING(
          logger(), "check_sub failed[{}]: rejected by on_check_sub_one type={} count={} guid={}, error={} ({})", i,
          type_id, sub_count, guid, result.error_code, PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
      return false;
    }

    FWINSTLOGDEBUG(logger(), "check_sub pass[{}]: type={} count={} guid={}", i, type_id, sub_count, guid);
    return true;
  });

  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request;
  }

  ItemOperationResult mode_result = on_check_sub(checked_request);
  if (mode_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    result = mode_result;
    FWINSTLOGWARNING(logger(), "check_sub rejected by mode, error={} ({})", result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
    return checked_request;
  }

  FWINSTLOGDEBUG(logger(), "check_sub pass, {} requests checked", checked_request.requests.size());
  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::sub(ItemSubCheckedRequest& checked_request,
                                                          ItemOperationReason reason) {
  if (!is_operation_allowed("ItemContainer::sub")) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }

  ItemOperationResult result = validate_checked_request(
      checked_request.result, checked_request.apply, checked_request.container_guid, checked_request.operate_id, "sub");
  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return result;
  }
  checked_request.apply = true;

  const ItemOperationContext context{reason, checked_request.source};

  size_t index = 0;
  ItemOperationResult one_result;
  checked_request.requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    size_t i = index++;
    one_result = on_sub_one(checked_request, request, context);
    if (one_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWINSTLOGERROR(logger(), "sub failed[{}]: error={} ({})", i, one_result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(one_result.error_code));
      return false;
    }
    return true;
  });
  if (one_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return one_result;
  }

  FWINSTLOGINFO(logger(), "sub success, {} requests applied", checked_request.requests.size());
  return result;
}

// ============================================================
// 移动 (move / check_move)
//
// 基础操作在基类: 建 checked request (request 自持) -> 初始化检查 -> 空请求判断 -> 模式校验
// -> 两个阶段 (kMoveSub 逐条扣来源, kMoveAdd 逐条放目标)。
// 位置语义 (来源 / 目标校验、批次内占用预演、索引维护) 由子类的三个钩子实现;
// 没有位置语义的模式不支持 move, 基类默认钩子直接拒绝。
// ============================================================

ITEM_ALGORITHM_API ItemMoveCheckedRequest ItemContainer::check_move(const excel_config_group_ptr_t& config_group,
                                                                    ItemMoveRequest&& request,
                                                                    const ItemOperationSource& source) const {
  ItemMoveCheckedRequest checked_request{config_group, std::move(request), get_container_guid(), issue_operate_id(),
                                         source};
  auto& result = checked_request.result;
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "check_move called before init, container_guid={} operate_id={}", get_container_guid(),
                   get_operate_id());
    result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN;
    return checked_request;
  }

  if (checked_request.request.move_sub_entrys.empty() && checked_request.request.move_add_entrys.empty()) {
    result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    FWINSTLOGWARNING(logger(), "check_move failed: empty move request, error={} ({})", result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(result.error_code));
    return checked_request;
  }

  // 本模式的移动校验 (入参 + 批次内占用预演): 失败时错误码由钩子写进 checked_request.result
  if (!on_check_move(checked_request)) {
    if (result.error_code == PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    }
    return checked_request;
  }

  FWINSTLOGDEBUG(logger(), "check_move pass, {} sub ops, {} add ops", checked_request.request.move_sub_entrys.size(),
                 checked_request.request.move_add_entrys.size());
  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::move(ItemMoveCheckedRequest& checked_request) {
  if (!is_operation_allowed("ItemContainer::move")) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }

  ItemOperationResult result =
      validate_checked_request(checked_request.result, checked_request.apply, checked_request.container_guid,
                               checked_request.operate_id, "move");
  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return result;
  }
  checked_request.apply = true;

  const ItemOperationContext sub_context{ItemOperationReason::kMoveSub, checked_request.source};
  const ItemOperationContext add_context{ItemOperationReason::kMoveAdd, checked_request.source};

  // Phase 1: 扣来源 (整体移走的条目在这里释放自己的位置与索引)
  ItemOperationResult one_result;
  size_t index = 0;
  for (const auto& sub_request : checked_request.request.move_sub_entrys) {
    size_t i = index++;
    one_result = on_move_sub_one(checked_request, sub_request, sub_context);
    if (one_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWINSTLOGERROR(logger(), "move sub failed[{}]: error={} ({})", i, one_result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(one_result.error_code));
      return one_result;
    }
  }

  // Phase 2: 放目标 (来源全部处理完再放, 同位置腾挪时不会被自己挡住)
  index = 0;
  for (const auto& add_request : checked_request.request.move_add_entrys) {
    size_t i = index++;
    one_result = on_move_add_one(checked_request, add_request, add_context);
    if (one_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWINSTLOGERROR(logger(), "move add failed[{}]: error={} ({})", i, one_result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(one_result.error_code));
      return one_result;
    }
  }

  FWINSTLOGINFO(logger(), "move success, {} sub ops, {} add ops", checked_request.request.move_sub_entrys.size(),
                checked_request.request.move_add_entrys.size());
  return result;
}

// ============================================================
// 移动钩子的默认实现 — 本模式没有位置语义时不支持 move
// ============================================================

ITEM_ALGORITHM_API bool ItemContainer::on_check_move(ItemMoveCheckedRequest& checked_request) const {
  checked_request.result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  FWINSTLOGWARNING(logger(), "check_move rejected: this container mode does not support move, container_guid={}",
                   get_container_guid());
  return false;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::on_move_sub_one(ItemMoveCheckedRequest& /*checked_request*/,
                                                                      const ItemMoveSubRequest& /*request*/,
                                                                      const ItemOperationContext& /*context*/) {
  FWINSTLOGERROR(logger(), "move sub rejected: this container mode does not support move, container_guid={}",
                 get_container_guid());
  return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::on_move_add_one(ItemMoveCheckedRequest& /*checked_request*/,
                                                                      const ItemMoveAddRequest& /*request*/,
                                                                      const ItemOperationContext& /*context*/) {
  FWINSTLOGERROR(logger(), "move add rejected: this container mode does not support move, container_guid={}",
                 get_container_guid());
  return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainer::check_has(const excel_config_group_ptr_t& config_group,
                                                                const item_basic_readable_iterable& requests) const {
  ItemOperationResult result;
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "check_has called before init, container_guid={}", get_container_guid());
    result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN;
    return result;
  }

  std::unordered_map<int32_t, int64_t> pending_type_required_count;
  size_t index = 0;

  // 回调返回 false 中断遍历; 错误码在返回 false 之前写进 result
  requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    size_t current_index = index++;

    int32_t check_ret = validate_item_basic(config_group, request);
    if (check_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = check_ret;
      result.failed_type_id = request.type_id();
      return false;
    }

    if (request.count() <= 0) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = request.type_id();
      return false;
    }

    int64_t total_count = count_type_total(request.type_id());
    int64_t required_count = pending_type_required_count[request.type_id()] + request.count();
    if (total_count < required_count) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
      result.failed_type_id = request.type_id();
      FWINSTLOGWARNING(logger(), "check_has failed[{}]: not enough type={}, total={} required={}", current_index,
                       request.type_id(), total_count, required_count);
      return false;
    }
    pending_type_required_count[request.type_id()] = required_count;

    // 单条业务校验 (业务容器覆盖 on_check_has_one)
    int32_t one_ret = on_check_has_one(config_group, request);
    if (one_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = one_ret;
      result.failed_type_id = request.type_id();
      FWINSTLOGWARNING(logger(), "check_has failed[{}]: rejected by on_check_has_one type={}", current_index,
                       request.type_id());
      return false;
    }

    return true;
  });

  if (result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return result;
  }

  ItemOperationResult mode_result = on_check_has(config_group, requests);
  if (mode_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWINSTLOGWARNING(logger(), "check_has rejected by mode, error={} ({})", mode_result.error_code,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(mode_result.error_code));
    return mode_result;
  }

  return result;
}

ITEM_ALGORITHM_API bool ItemContainer::load(const excel_config_group_ptr_t& config_group,
                                            const PROJECT_NAMESPACE_ID::DItemInstance& item_instance,
                                            ItemOperationReason reason, const ItemOperationSource& source) {
  if (!is_operation_allowed("ItemContainer::load")) {
    return false;
  }
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "load called before init, container_guid={} operate_id={}", get_container_guid(),
                   get_operate_id());
    return false;
  }

  const auto& item_basic = item_instance.item_basic();
  int32_t type_id = item_basic.type_id();
  int64_t add_count = item_basic.count();
  int64_t guid = item_basic.guid();

  int32_t check_ret = validate_item_basic(config_group, item_basic);
  if (check_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWINSTLOGWARNING(logger(), "load failed: invalid item type={} count={} guid={}, error={} ({})", type_id, add_count,
                     guid, check_ret, PROJECT_NAMESPACE_ID::EnErrorCode_Name(check_ret));
    return false;
  }

  // 载入的是持久化数据, 可能与容器内已有数据冲突, 这里按 GUID 唯一兜底
  if (guid != 0 && has_entry_guid(guid)) {
    FWINSTLOGWARNING(logger(), "load failed: duplicate guid={} type={} count={}", guid, type_id, add_count);
    return false;
  }

  int64_t current_total = get_item_count(type_id);
  int32_t limit_ret = on_check_item_count_limit(type_id, current_total, add_count);
  if (limit_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    FWINSTLOGWARNING(logger(), "load failed: count limit exceeded type={} current={} add={}, error={} ({})", type_id,
                     current_total, add_count, limit_ret, PROJECT_NAMESPACE_ID::EnErrorCode_Name(limit_ret));
    return false;
  }

  // 钩子上下文: 原因由调用方指定 (默认 kLoad), 来源由调用方传入
  const ItemOperationContext context{reason, source};
  return on_load_one(config_group, item_instance, context);
}

ITEM_ALGORITHM_API void ItemContainer::apply_entries(const excel_config_group_ptr_t& config_group,
                                                     const google::protobuf::RepeatedField<uint64_t>& remove_entry_ids,
                                                     const item_instance_entry_readable_iterable& update_entries,
                                                     const ItemOperationSource& source) {
  if (!is_operation_allowed("ItemContainer::apply_entries")) {
    return;
  }
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "apply_entries called before init, container_guid={} operate_id={}", get_container_guid(),
                   get_operate_id());
    return;
  }

  // 两个阶段各自的钩子上下文: 原因由本接口固定, 来源由调用方传入
  const ItemOperationContext remove_context{ItemOperationReason::kApplyRemove, source};
  const ItemOperationContext update_context{ItemOperationReason::kApplyUpdate, source};

  // ============================================================
  // Phase 1: 按 entry_id 删除
  // ============================================================
  for (uint64_t remove_id : remove_entry_ids) {
    item_entry_ptr_t found = find_entry_by_id(remove_id);
    if (!found) {
      FWINSTLOGERROR(logger(), "apply remove failed: entry_id={} not found", remove_id);
      continue;
    }

    int32_t type_id = found->item_instance().item_basic().type_id();
    int64_t old_count = found->item_instance().item_basic().count();

    on_apply_remove_one(config_group, found, remove_context);
    FWINSTLOGINFO(logger(), "apply remove entry_id={} type={} count={}", remove_id, type_id, old_count);
  }

  // ============================================================
  // Phase 2: 按 entry_id 新增或更新
  // ============================================================
  // 回调返回 false 会中断遍历; 跳过本条 (相当于原来循环里的 continue) 时返回 true 继续
  update_entries.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update) -> bool {
    const auto& item_basic = update.instance().item_basic();
    int32_t type_id = item_basic.type_id();

    if (update.entry_id() == 0) {
      FWINSTLOGERROR(logger(), "apply update failed: entry_id=0 type={}", type_id);
      return true;
    }

    // 同步包是外部输入, 必须自己校验: 道具字段合法且归属本容器。
    // 少了这一步就会把别的容器的数据或数量为 0 的条目写进本容器。
    int32_t check_ret = validate_item_basic(config_group, item_basic);
    if (check_ret != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      FWINSTLOGERROR(logger(), "apply update failed: invalid item entry_id={} type={} count={} error={} ({})",
                     update.entry_id(), type_id, item_basic.count(), check_ret,
                     PROJECT_NAMESPACE_ID::EnErrorCode_Name(check_ret));
      return true;
    }

    item_entry_ptr_t existing = find_entry_by_id(update.entry_id());
    on_apply_update_one(config_group, existing, update, update_context);
    FWINSTLOGINFO(logger(), "apply {} entry_id={} type={} count={}", existing ? "update" : "add", update.entry_id(),
                  type_id, item_basic.count());
    return true;
  });

  FWINSTLOGINFO(logger(), "apply entries done, {} remove, {} update", remove_entry_ids.size(), update_entries.size());
}

ITEM_ALGORITHM_API bool ItemContainer::find_positions_for_instances(
    const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
    const item_basic_readable_iterable& ignore_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const {
  if (!is_initialized()) {
    FWINSTLOGERROR(logger(), "find_positions called before init, container_guid={}", get_container_guid());
    return false;
  }

  // 输出预留: 成功项最多与输入等长, 失败项最多与输入等长
  success_item.Reserve(static_cast<int>(success_item.size() + items.size()));
  failed_item.Reserve(static_cast<int>(failed_item.size() + items.size()));

  return on_find_positions(config_group, items, ignore_item, success_item, failed_item);
}

ITEM_ALGORITHM_API bool ItemContainer::find_positions_for_basics(
    const excel_config_group_ptr_t& config_group, const item_basic_readable_iterable& basics,
    const item_basic_readable_iterable& ignore_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed_item) const {
  // 寻位只在内部坐标上推演, 所以基类把 DItemBasic 包成 DItemInstance 后复用实例版,
  // 再把结果里的 item_basic 拆回 DItemBasic, 子类只需要实现一份寻位。
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> wrapped_items;
  wrapped_items.Reserve(static_cast<int>(basics.size()));
  basics.foreach ([&wrapped_items](const PROJECT_NAMESPACE_ID::DItemBasic& basic) -> bool {
    *wrapped_items.Add()->mutable_item_basic() = basic;
    return true;
  });

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> wrapped_success;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> wrapped_failed;
  if (!find_positions_for_instances(config_group, make_item_readable_iterable(wrapped_items), ignore_item,
                                    wrapped_success, wrapped_failed)) {
    return false;
  }

  success_item.Reserve(static_cast<int>(success_item.size() + wrapped_success.size()));
  for (const auto& item : wrapped_success) {
    *success_item.Add() = item.item_basic();
  }

  failed_item.Reserve(static_cast<int>(failed_item.size() + wrapped_failed.size()));
  for (const auto& item : wrapped_failed) {
    *failed_item.Add() = item.item_basic();
  }
  return true;
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
