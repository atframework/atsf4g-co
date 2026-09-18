// Copyright 2026 atframework

#include "ItemAlgorithm/Container/ItemInfiniteGridContainer.h"

#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

ITEM_ALGORITHM_API ItemInfiniteGridContainer::ItemInfiniteGridContainer() {}

ITEM_ALGORITHM_API ItemInfiniteGridContainer::~ItemInfiniteGridContainer() {}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::init(
    PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type, int64_t container_guid) {
  init_container(container_guid);
  position_type_ = position_type;

  // 不限制行列, 也不维护位图: 位置占用只由子类的 position_index_ 表达 (每件道具占一个位置)
  FWINSTLOGDEBUG(logger(), "init infinite grid position_type={} container_guid={}",
                 static_cast<int>(position_type_), container_guid);
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::copy_empty_config_to(ItemContainer& out) const {
  auto* infinite_out = dynamic_cast<ItemInfiniteGridContainer*>(&out);
  if (nullptr == infinite_out) {
    FWINSTLOGERROR(logger(), "copy_empty_config_to target is not ItemInfiniteGridContainer, container_guid={}",
                   get_container_guid());
    return;
  }

  infinite_out->init_container(get_container_guid());
  infinite_out->position_type_ = position_type_;
}

// ============================================================
// 位置与身份索引 (单槽位: 每个位置最多一个条目)
// ============================================================

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::on_clear() {
  position_index_.clear();
  guid_index_.clear();
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemInfiniteGridContainer::find_entry_at_position(
    const ItemGridPosition& position) const {
  auto it = position_index_.find(position);
  return (it != position_index_.end()) ? it->second : nullptr;
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::attach_entry_position(const ItemGridPosition& position,
                                                                        const item_entry_ptr_t& entry) {
  position_index_[position] = entry;
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::detach_entry_position(const ItemGridPosition& position) {
  position_index_.erase(position);
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemInfiniteGridContainer::find_entry_by_guid(int64_t guid) const {
  auto it = guid_index_.find(guid);
  return (it != guid_index_.end()) ? it->second : nullptr;
}

ITEM_ALGORITHM_API bool ItemInfiniteGridContainer::has_entry_guid(int64_t guid) const {
  return guid_index_.find(guid) != guid_index_.end();
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::attach_entry_guid(int64_t guid, const item_entry_ptr_t& entry) {
  guid_index_[guid] = entry;
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::detach_entry_guid(int64_t guid) { guid_index_.erase(guid); }

// ============================================================
// 索引维护 (本模式的操作流程调用)
//
// attach_entry_index / detach_entry_index 是位置配置无关的版本 (本模式每件道具固定占一个
// 槽位, 不在库里读位置配置), 客户端同步这类拿不到配置组的钩子用它;
// add_entry_index / remove_entry_index 保留带配置的调用形状给 check_move 这类流程
// ============================================================

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::attach_entry_index(const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance().item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance().item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    // 不维护位图: 位置占用只由 position_index_ 表达
    attach_entry_position(pos, entry);
  }

  int64_t guid = entry->item_instance().item_basic().guid();
  if (guid != 0) {
    attach_entry_guid(guid, entry);
  }
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::detach_entry_index(const item_entry_ptr_t& entry) {
  if (!entry) {
    return;
  }

  ItemGridPosition pos = extract_position(entry->item_instance().item_basic().position().grid_position());
  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(entry->item_instance().item_basic().type_id());
  if (item_type_config && item_type_config->need_occupy_the_grid) {
    detach_entry_position(pos);
  }

  int64_t guid = entry->item_instance().item_basic().guid();
  if (guid != 0) {
    detach_entry_guid(guid);
  }
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::add_entry_index(
    const PROJECT_NAMESPACE_ID::DItemPositionCfg& /*position_cfg*/, const item_entry_ptr_t& entry) {
  attach_entry_index(entry);
}

ITEM_ALGORITHM_API void ItemInfiniteGridContainer::remove_entry_index(
    const PROJECT_NAMESPACE_ID::DItemPositionCfg& /*position_cfg*/, const item_entry_ptr_t& entry) {
  detach_entry_index(entry);
}

// ============================================================
// 模式相关公开接口 (本模式实现)
// ============================================================

ITEM_ALGORITHM_API bool ItemInfiniteGridContainer::on_find_position_for_infinite(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& /*config_group*/,
    const PROJECT_NAMESPACE_ID::DItemBasic& /*basic*/, PROJECT_NAMESPACE_ID::DItemGridPosition& /*out_pos*/) const {
  // 默认无法自动确定位置; 具体业务容器 (如装备槽按 type_id 映射槽位) 覆盖本函数
  return false;
}

ITEM_ALGORITHM_API uint64_t ItemInfiniteGridContainer::make_entry_sort_key(
    const PROJECT_NAMESPACE_ID::DItemInstance& instance) const {
  // 位置知识留在容器这一侧: 条目只保存这个不透明键。
  // 坐标经接入层实现的 extract_position 从 proto 位置字段取出, 库内不解析具体字段
  ItemGridPosition pos = extract_position(instance.item_basic().position().grid_position());
  return (static_cast<uint64_t>(pos.y) << 32) + (static_cast<uint64_t>(pos.x) & 0xFFFFFFFF);
}

// ============================================================
// 条目校验 (本模式实现)
// ============================================================

ITEM_ALGORITHM_API bool ItemInfiniteGridContainer::is_item_valid(
    const excel_config_group_ptr_t& config_group, const PROJECT_NAMESPACE_ID::DItemBasic& basic) const {
  if (basic.type_id() == 0 || basic.count() <= 0) {
    return false;
  }

  auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
  if (item_type_config == nullptr) {
    return false;
  }

  // 无限格子(槽位)只放占格道具, 且必须有位置配置
  if (!item_type_config->need_occupy_the_grid) {
    return false;
  }
  if (get_item_position_cfg(config_group, basic) == nullptr) {
    return false;
  }

  if (item_type_config->need_guid) {
    if (basic.guid() == 0) {
      return false;
    }
  } else if (basic.guid() != 0) {
    return false;
  }
  return true;
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemInfiniteGridContainer::get(
    const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const {
  return find_entry_at_position(extract_position(position));
}

ITEM_ALGORITHM_API item_entry_ptr_t ItemInfiniteGridContainer::get_by_guid(int64_t guid) const {
  return find_entry_by_guid(guid);
}

// ============================================================
// 寻位 (本模式不扫描格子: 逐件交给 on_find_position_for_infinite 决定落位)
//
// 批次流程在基类 (find_positions_for_instances 做入参检查与输出预留, 并把
// DItemBasic 版包装成实例版复用这里), 这里只负责逐件的落位推演。
// ============================================================

ITEM_ALGORITHM_API bool ItemInfiniteGridContainer::on_find_positions(
    const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
    const item_basic_readable_iterable& ignore_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const {
  // 输入已经是一层视图, 取不到底层容器, 因此只能拦住两个输出参数是同一个对象的情况
  if (&success_item == &failed_item) {
    return false;
  }

  // 本模式没有格子与占用可推算, 无法表达"某个道具即将被消耗, 请把它占的槽位当成空闲"
  if (!ignore_item.empty()) {
    FWINSTLOGWARNING(logger(), "find_positions failed: ignore_item is not supported in infinite grid mode, count={}",
                     ignore_item.size());
    return false;
  }

  // foreach 返回 false 表示提前中断: 这里只有"道具非法"一条中断路径, 中断后整次寻位失败
  return items.foreach([&](const PROJECT_NAMESPACE_ID::DItemInstance& item) {
    const auto& basic = item.item_basic();

    auto item_type_config = ItemAlgorithmTypeOption::GetItemType(basic.type_id());
    if (item_type_config == nullptr || !is_item_valid(config_group, basic)) {
      FWINSTLOGWARNING(logger(), "find_positions failed: invalid item type={} count={} guid={}", basic.type_id(),
                       basic.count(), basic.guid());
      return false;
    }

    // 复用同一个临时实例保存候选位置
    PROJECT_NAMESPACE_ID::DItemInstance tmp_inst;
    *tmp_inst.mutable_item_basic() = basic;

    PROJECT_NAMESPACE_ID::DItemGridPosition out_pos;
    bool found = on_find_position_for_infinite(config_group, basic, out_pos);
    if (found) {
      *tmp_inst.mutable_item_basic()->mutable_position()->mutable_grid_position() = out_pos;
      tmp_inst.mutable_item_basic()->mutable_position()->set_container_guid(get_container_guid());
      // 候选位置还要过一遍单条业务校验 (业务容器覆盖 on_check_add_one); 不通过就进 failed_item
      if (PROJECT_NAMESPACE_ID::EN_SUCCESS != on_check_add_one(config_group, tmp_inst)) {
        FWINSTLOGWARNING(logger(), "find_positions: on_check_add_one rejected type={}, move to failed_item",
                         basic.type_id());
        found = false;
      }
    }

    if (found) {
      auto* out = success_item.Add();
      *out = item;
      auto* out_basic = out->mutable_item_basic();
      out_basic->set_count(basic.count());
      *out_basic->mutable_position()->mutable_grid_position() = out_pos;
      // 与其它模式一致地写回容器归属, 否则结果无法直接喂给 check_add
      out_basic->mutable_position()->set_container_guid(get_container_guid());
    } else {
      // 子类无法确定位置: 交给调用方决定后续处理
      FWINSTLOGWARNING(logger(),
                       "find_positions: on_find_position_for_infinite rejected type={}, move to failed_item",
                       basic.type_id());
      auto* out = failed_item.Add();
      *out = item;
      out->mutable_item_basic()->set_count(basic.count());
    }

    return true;
  });
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
