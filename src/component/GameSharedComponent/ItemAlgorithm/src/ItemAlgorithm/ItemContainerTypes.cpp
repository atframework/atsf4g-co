// Copyright 2026 atframework

#include "ItemAlgorithm/ItemContainerTypes.h"

#include <utility>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 容器级 Checked Request 特殊成员实现 (导出符号, 供 DLL 外构造/移动)
// ============================================================

ITEM_ALGORITHM_API ItemAddCheckedRequest::ItemAddCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    item_instance_readable_iterable& in_requests, int64_t in_container_guid, int64_t in_operate_id,
    const ItemOperationSource& in_source)
    : config_group(in_config_group),
      requests(in_requests),
      source(in_source),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemAddCheckedRequest::ItemAddCheckedRequest(ItemAddCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(other.requests),
      result(other.result),
      source(other.source),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemSubCheckedRequest::ItemSubCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    item_basic_readable_iterable& in_requests, int64_t in_container_guid, int64_t in_operate_id,
    const ItemOperationSource& in_source)
    : config_group(in_config_group),
      requests(in_requests),
      source(in_source),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemSubCheckedRequest::ItemSubCheckedRequest(ItemSubCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(other.requests),
      result(other.result),
      source(other.source),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemMoveCheckedRequest::ItemMoveCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    ItemMoveRequest&& in_request, int64_t in_container_guid, int64_t in_operate_id,
    const ItemOperationSource& in_source)
    : config_group(in_config_group),
      request(std::move(in_request)),
      source(in_source),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemMoveCheckedRequest::ItemMoveCheckedRequest(ItemMoveCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      request(std::move(other.request)),
      result(other.result),
      source(other.source),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

ITEM_ALGORITHM_API ItemReplaceCheckedRequest::ItemReplaceCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    item_instance_readable_iterable& in_requests, int64_t in_container_guid, int64_t in_operate_id,
    const ItemOperationSource& in_source)
    : config_group(in_config_group),
      requests(in_requests),
      source(in_source),
      container_guid(in_container_guid),
      operate_id(in_operate_id) {}

ITEM_ALGORITHM_API ItemReplaceCheckedRequest::ItemReplaceCheckedRequest(ItemReplaceCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(other.requests),
      result(other.result),
      source(other.source),
      apply(other.apply),
      container_guid(other.container_guid),
      operate_id(other.operate_id) {}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
