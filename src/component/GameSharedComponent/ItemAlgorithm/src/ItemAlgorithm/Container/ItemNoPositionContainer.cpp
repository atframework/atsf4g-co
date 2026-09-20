// Copyright 2026 atframework

#include "ItemAlgorithm/Container/ItemNoPositionContainer.h"

#include <config/excel/item_type_config.h>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 无位置容器 — 本模式的单条行为与数据结构
//
// 增删改查的批次流程在基类 ItemContainer, 基类逐条路由到本文件的钩子;
// proto 位置字段的读写由接入层实现 (extract_position / apply_position), 库内不做映射。
// ============================================================

ITEM_ALGORITHM_API ItemNoPositionContainer::ItemNoPositionContainer() {}

ITEM_ALGORITHM_API ItemNoPositionContainer::~ItemNoPositionContainer() {}

ITEM_ALGORITHM_API void ItemNoPositionContainer::init(
    PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid) {
  init_container(container_guid);
  position_type_ = position_type;

  // 无位置模式不落位: 道具按类型合并计数, 不维护位置索引, 也不接受 GUID
  FWINSTLOGDEBUG(logger(), "init no position container position_type={} container_guid={}",
                 static_cast<int>(position_type_), container_guid);
}

// ============================================================
// 条目校验 (基类的 validate_item_basic 依赖本函数)
// ============================================================

ITEM_ALGORITHM_API bool ItemNoPositionContainer::is_item_valid(
    const excel_config_group_ptr_t& /*config_group*/, const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  if (basic.type_id() == 0 || basic.count() <= 0) {
    return false;
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
  if (item_type_config == nullptr) {
    return false;
  }

  // 无位置模式: 不接受占格道具 (没地方放), 也不接受 GUID (没有位置就不允许有 guid)
  if (item_type_config->need_occupy_the_grid || item_type_config->need_guid || basic.guid() != 0) {
    return false;
  }
  return true;
}

// ============================================================
// 批次流程钩子
// ============================================================

ITEM_ALGORITHM_API bool ItemNoPositionContainer::should_skip_add_request(
    const PROJECT_NAMESPACE_ID::DItemInstance& request) const {
  // 本模式只按类型计数: count == 0 的请求没有可加的数量, 属于空请求, check 与执行都跳过。
  // (count < 0 仍按非法数据处理, 由 is_item_valid 拒绝)
  return request.item_basic().count() == 0;
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
