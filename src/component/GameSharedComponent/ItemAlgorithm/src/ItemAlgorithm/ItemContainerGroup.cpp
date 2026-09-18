// Copyright 2026 atframework

#include "ItemAlgorithm/ItemContainerGroup.h"

#include <ItemAlgorithm/ItemContainerEntry.h>

#include <unordered_map>
#include <utility>
#include <vector>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

namespace {

/// @brief 按首次出现顺序收集 per-container 分片
///
/// 组级请求按 position 分片时, 同一个容器的请求要攒在同一个分片里; 分片顺序按容器第一次
/// 出现的顺序固定 (执行顺序可预期, 也方便排查问题)。
template <class PerContainerDataT>
class container_part_collector {
 public:
  using data_ptr_t = atfw::util::memory::strong_rc_ptr<PerContainerDataT>;

  /// @brief 取(必要时新建)某个容器的分片
  data_ptr_t& get_or_create(const item_container_ptr_t& container) {
    auto iter = index_.find(container.get());
    if (iter != index_.end()) {
      return parts_[iter->second];
    }

    index_[container.get()] = parts_.size();
    parts_.push_back(atfw::util::memory::make_strong_rc<PerContainerDataT>(container));
    return parts_.back();
  }

  std::vector<data_ptr_t>& parts() { return parts_; }

 private:
  std::vector<data_ptr_t> parts_;
  std::unordered_map<const ItemContainer*, size_t> index_;
};

}  // namespace

// ============================================================
// PerContainerData
// ============================================================

ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest::PerContainerData::PerContainerData(
    item_container_ptr_t in_container)
    : container(std::move(in_container)) {}

ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest::PerContainerData::PerContainerData(
    item_container_ptr_t in_container)
    : container(std::move(in_container)) {}

ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest::PerContainerData::PerContainerData(
    item_container_ptr_t in_container)
    : container(std::move(in_container)) {}

ITEM_ALGORITHM_API ItemContainerGroupReplaceCheckedRequest::PerContainerData::PerContainerData(
    item_container_ptr_t in_container)
    : container(std::move(in_container)) {}

// ============================================================
// 组级 checked request 的特殊成员 (导出符号, 供 DLL 外构造/移动)
// ============================================================

ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest::ItemContainerGroupAddCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group)
    : config_group(in_config_group) {}

ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest::ItemContainerGroupAddCheckedRequest(
    ItemContainerGroupAddCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      result(other.result),
      container_data(std::move(other.container_data)),
      apply(other.apply) {}

ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest& ItemContainerGroupAddCheckedRequest::operator=(
    ItemContainerGroupAddCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    result = other.result;
    container_data = std::move(other.container_data);
    apply = other.apply;
  }
  return *this;
}

ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest::ItemContainerGroupSubCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group)
    : config_group(in_config_group) {}

ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest::ItemContainerGroupSubCheckedRequest(
    ItemContainerGroupSubCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      result(other.result),
      container_data(std::move(other.container_data)),
      apply(other.apply) {}

ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest& ItemContainerGroupSubCheckedRequest::operator=(
    ItemContainerGroupSubCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    result = other.result;
    container_data = std::move(other.container_data);
    apply = other.apply;
  }
  return *this;
}

ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest::ItemContainerGroupMoveCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
    std::vector<ItemContainerGroupMoveRequest>&& in_requests)
    : config_group(in_config_group), requests(std::move(in_requests)) {}

ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest::ItemContainerGroupMoveCheckedRequest(
    ItemContainerGroupMoveCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      requests(std::move(other.requests)),
      result(other.result),
      container_data(std::move(other.container_data)),
      apply(other.apply) {}

ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest& ItemContainerGroupMoveCheckedRequest::operator=(
    ItemContainerGroupMoveCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    requests = std::move(other.requests);
    result = other.result;
    container_data = std::move(other.container_data);
    apply = other.apply;
  }
  return *this;
}

ITEM_ALGORITHM_API ItemContainerGroupReplaceCheckedRequest::ItemContainerGroupReplaceCheckedRequest(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group)
    : config_group(in_config_group) {}

ITEM_ALGORITHM_API ItemContainerGroupReplaceCheckedRequest::ItemContainerGroupReplaceCheckedRequest(
    ItemContainerGroupReplaceCheckedRequest&& other) noexcept
    : config_group(std::move(other.config_group)),
      result(other.result),
      container_data(std::move(other.container_data)),
      apply(other.apply) {}

ITEM_ALGORITHM_API ItemContainerGroupReplaceCheckedRequest& ItemContainerGroupReplaceCheckedRequest::operator=(
    ItemContainerGroupReplaceCheckedRequest&& other) noexcept {
  if (this != &other) {
    config_group = std::move(other.config_group);
    result = other.result;
    container_data = std::move(other.container_data);
    apply = other.apply;
  }
  return *this;
}

// ============================================================
// ItemContainerGroup
// ============================================================

ITEM_ALGORITHM_API ItemContainerGroup::ItemContainerGroup() {}
ITEM_ALGORITHM_API ItemContainerGroup::~ItemContainerGroup() {}

// ---- Add ----

ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest ItemContainerGroup::check_add(
    const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& requests,
    const ItemOperationSource& source) const {
  ItemContainerGroupAddCheckedRequest checked_request(config_group);
  auto& result = checked_request.result;

  // Phase 1: 按 position 分片 (同容器的请求攒在同一个分片里)
  container_part_collector<ItemContainerGroupAddCheckedRequest::PerContainerData> collector;
  bool group_ok = requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& request) -> bool {
    int32_t type_id = request.item_basic().type_id();
    item_container_ptr_t container = select_container(request.item_basic().position());
    if (container == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      return false;
    }

    *collector.get_or_create(container)->requests.Add() = request;
    return true;
  });
  if (!group_ok) {
    return checked_request;
  }

  // Phase 2: 逐容器 check_add (任一失败即整体失败, 不产生任何变更)
  checked_request.container_data = std::move(collector.parts());
  for (auto& part : checked_request.container_data) {
    auto container_checked = part->container->check_add(config_group, part->requests, source);
    if (container_checked.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = container_checked.result.error_code;
      result.failed_type_id = container_checked.result.failed_type_id;
      checked_request.container_data.clear();
      return checked_request;
    }
    part->checked.reset(new ItemAddCheckedRequest(std::move(container_checked)));
  }

  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainerGroup::add(ItemContainerGroupAddCheckedRequest& checked_request) {
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request.result;
  }
  if (checked_request.apply) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  checked_request.apply = true;

  // 逐容器执行: 第一个失败的容器结果直接返回 (前面已经落地的分片不回滚)
  for (const auto& part : checked_request.container_data) {
    ItemOperationResult part_result = part->container->add(*part->checked);
    if (part_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return part_result;
    }
  }

  return checked_request.result;
}

// ---- Sub ----

ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest
ItemContainerGroup::check_sub(const excel_config_group_ptr_t& config_group,
                              const item_basic_readable_iterable& requests, const ItemOperationSource& source) const {
  ItemContainerGroupSubCheckedRequest checked_request(config_group);
  auto& result = checked_request.result;

  container_part_collector<ItemContainerGroupSubCheckedRequest::PerContainerData> collector;
  bool group_ok = requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    int32_t type_id = request.type_id();
    item_container_ptr_t container = select_container(request.position());
    if (container == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      return false;
    }

    *collector.get_or_create(container)->requests.Add() = request;
    return true;
  });
  if (!group_ok) {
    return checked_request;
  }

  checked_request.container_data = std::move(collector.parts());
  for (auto& part : checked_request.container_data) {
    auto container_checked = part->container->check_sub(config_group, part->requests, source);
    if (container_checked.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = container_checked.result.error_code;
      result.failed_type_id = container_checked.result.failed_type_id;
      checked_request.container_data.clear();
      return checked_request;
    }
    part->checked.reset(new ItemSubCheckedRequest(std::move(container_checked)));
  }

  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainerGroup::sub(ItemContainerGroupSubCheckedRequest& checked_request) {
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request.result;
  }
  if (checked_request.apply) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  checked_request.apply = true;

  for (const auto& part : checked_request.container_data) {
    ItemOperationResult part_result = part->container->sub(*part->checked);
    if (part_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return part_result;
    }
  }

  return checked_request.result;
}

// ---- Move ----

ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest ItemContainerGroup::check_move(
    const excel_config_group_ptr_t& config_group, std::vector<ItemContainerGroupMoveRequest>&& requests,
    const ItemOperationSource& source) const {
  ItemContainerGroupMoveCheckedRequest checked_request(config_group, std::move(requests));
  auto& result = checked_request.result;
  auto& group_requests = checked_request.requests;

  if (group_requests.empty()) {
    result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    return checked_request;
  }

  // ============================================================
  // Phase 1: 把组级移动请求翻译成每个容器的 ItemMoveRequest
  //
  //   - 同容器同锚点: 不需要移动, 跳过
  //   - 同容器: sub + add 都放进该容器的请求 (同一个源条目的 sub 合并 op_count)
  //   - 跨容器: 源容器只 sub, 目标容器只 add
  // ============================================================
  struct ContainerBuilder {
    item_container_ptr_t container;
    ItemMoveRequest move_request;
    /// 同一源条目的 sub 在 move_sub_entrys 里的下标 (合并用)
    std::unordered_map<const ItemEntry*, size_t> sub_merge_index;
  };

  std::vector<ContainerBuilder> builders;
  std::unordered_map<const ItemContainer*, size_t> builder_index;
  // 同一源条目在本次移动里还剩多少可移动 (跨多条组请求时用来判断整体 / 部分移动)
  std::unordered_map<const ItemEntry*, int64_t> source_remaining;

  auto get_builder = [&](const item_container_ptr_t& container) -> ContainerBuilder& {
    auto iter = builder_index.find(container.get());
    if (iter != builder_index.end()) {
      return builders[iter->second];
    }

    builder_index[container.get()] = builders.size();
    builders.emplace_back();
    builders.back().container = container;
    return builders.back();
  };

  // 追加一条 sub (同一条目合并数量)
  auto append_sub = [](ContainerBuilder& builder, const item_entry_ptr_t& entry, int32_t op_count) {
    auto sub_iter = builder.sub_merge_index.find(entry.get());
    if (sub_iter != builder.sub_merge_index.end()) {
      builder.move_request.move_sub_entrys[sub_iter->second].op_count += op_count;
      return;
    }

    builder.sub_merge_index[entry.get()] = builder.move_request.move_sub_entrys.size();
    ItemMoveSubRequest sub_request;
    sub_request.entry = entry;
    sub_request.op_count = op_count;
    builder.move_request.move_sub_entrys.push_back(std::move(sub_request));
  };

  // 追加一条 add (数据模板用独立条目, 避免容器层的条目去重判断失败)
  auto append_add = [](ContainerBuilder& builder, const item_container_ptr_t& owner, const item_entry_ptr_t& source,
                       const PROJECT_NAMESPACE_ID::DItemPosition& goal_position, int32_t op_count, bool is_whole_move) {
    PROJECT_NAMESPACE_ID::DItemInstance add_instance = source->item_instance();
    add_instance.mutable_item_basic()->set_count(op_count);
    if (!is_whole_move) {
      // 部分移动时来源条目还在, GUID 不能跟着复制到新条目
      add_instance.mutable_item_basic()->set_guid(0);
    }
    *add_instance.mutable_item_basic()->mutable_position() = goal_position;

    ItemMoveAddRequest add_request;
    add_request.entry = atfw::util::memory::make_strong_rc<ItemEntry>(owner, std::move(add_instance), 0);
    add_request.goal_position = goal_position;
    add_request.op_count = op_count;
    builder.move_request.move_add_entrys.push_back(std::move(add_request));
  };

  for (const auto& group_request : group_requests) {
    int32_t type_id = group_request.source_item_basic.type_id();
    int64_t move_count = group_request.source_item_basic.count();
    if (type_id == 0 || move_count <= 0) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      return checked_request;
    }

    item_container_ptr_t source_container = select_container(group_request.source_item_basic.position());
    item_container_ptr_t target_container = select_container(group_request.target_position);
    if (source_container == nullptr || target_container == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      return checked_request;
    }

    item_entry_ptr_t source_entry = source_container->find_entry(group_request.source_item_basic);
    if (!source_entry) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_FOUND;
      result.failed_type_id = type_id;
      return checked_request;
    }

    // 同一条目跨多条组请求时按剩余数量递减, 超出即为数量不足
    auto& remaining = source_remaining[source_entry.get()];
    if (remaining == 0) {
      remaining = source_entry->item_instance().item_basic().count();
    }
    if (move_count > remaining) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
      result.failed_type_id = type_id;
      return checked_request;
    }
    bool is_whole_move = (move_count == remaining);
    remaining -= move_count;
    int32_t op_count = static_cast<int32_t>(move_count);

    // 同一容器同一锚点: 不需要移动
    if (source_container == target_container &&
        source_container->is_same_grid_position(group_request.source_item_basic.position().grid_position(),
                                                group_request.target_position.grid_position())) {
      continue;
    }

    append_sub(get_builder(source_container), source_entry, op_count);
    append_add(get_builder(target_container), target_container, source_entry, group_request.target_position, op_count,
               is_whole_move);
  }

  // ============================================================
  // Phase 2: 逐容器 check_move (任一失败即整体失败), 通过的分片留给 move 执行
  // ============================================================
  checked_request.container_data.reserve(builders.size());
  for (auto& builder : builders) {
    if (builder.move_request.move_sub_entrys.empty() && builder.move_request.move_add_entrys.empty()) {
      continue;
    }

    auto container_checked = builder.container->check_move(config_group, std::move(builder.move_request), source);
    if (container_checked.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = container_checked.result.error_code;
      result.failed_type_id = container_checked.result.failed_type_id;
      checked_request.container_data.clear();
      return checked_request;
    }

    auto part =
        atfw::util::memory::make_strong_rc<ItemContainerGroupMoveCheckedRequest::PerContainerData>(builder.container);
    part->checked.reset(new ItemMoveCheckedRequest(std::move(container_checked)));
    checked_request.container_data.push_back(std::move(part));
  }

  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult ItemContainerGroup::move(ItemContainerGroupMoveCheckedRequest& checked_request) {
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request.result;
  }
  if (checked_request.apply) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  checked_request.apply = true;

  // 分片顺序 = 容器第一次出现的顺序: 跨容器移动时来源容器先扣, 目标容器再放
  for (const auto& part : checked_request.container_data) {
    ItemOperationResult part_result = part->container->move(*part->checked);
    if (part_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return part_result;
    }
  }

  return checked_request.result;
}

// ---- Replace ----

ITEM_ALGORITHM_API ItemContainerGroupReplaceCheckedRequest ItemContainerGroup::check_replace(
    const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& requests,
    const ItemOperationSource& source) const {
  ItemContainerGroupReplaceCheckedRequest checked_request(config_group);
  auto& result = checked_request.result;

  container_part_collector<ItemContainerGroupReplaceCheckedRequest::PerContainerData> collector;
  bool group_ok = requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemInstance& request) -> bool {
    int32_t type_id = request.item_basic().type_id();
    item_container_ptr_t container = select_container(request.item_basic().position());
    if (container == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      return false;
    }

    *collector.get_or_create(container)->requests.Add() = request;
    return true;
  });
  if (!group_ok) {
    return checked_request;
  }

  checked_request.container_data = std::move(collector.parts());
  for (auto& part : checked_request.container_data) {
    auto container_checked = part->container->check_replace(config_group, part->requests, source);
    if (container_checked.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      result.error_code = container_checked.result.error_code;
      result.failed_type_id = container_checked.result.failed_type_id;
      checked_request.container_data.clear();
      return checked_request;
    }
    part->checked.reset(new ItemReplaceCheckedRequest(std::move(container_checked)));
  }

  return checked_request;
}

ITEM_ALGORITHM_API ItemOperationResult
ItemContainerGroup::replace(ItemContainerGroupReplaceCheckedRequest& checked_request) {
  if (checked_request.result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
    return checked_request.result;
  }
  if (checked_request.apply) {
    return {PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, 0};
  }
  checked_request.apply = true;

  for (const auto& part : checked_request.container_data) {
    ItemOperationResult part_result = part->container->replace(*part->checked);
    if (part_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return part_result;
    }
  }

  return checked_request.result;
}

// ---- Has ----

ITEM_ALGORITHM_API ItemOperationResult ItemContainerGroup::check_has(
    const excel_config_group_ptr_t& config_group, const item_basic_readable_iterable& requests) const {
  ItemOperationResult result;

  // check_has 不产生 checked request, 分片按容器攒好之后就地检查
  struct HasPart {
    item_container_ptr_t container;
    item_container_group_request_list<PROJECT_NAMESPACE_ID::DItemBasic> requests;
  };
  std::vector<HasPart> parts;
  std::unordered_map<const ItemContainer*, size_t> part_index;

  bool group_ok = requests.foreach ([&](const PROJECT_NAMESPACE_ID::DItemBasic& request) -> bool {
    int32_t type_id = request.type_id();
    item_container_ptr_t container = select_container(request.position());
    if (container == nullptr) {
      result.error_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      result.failed_type_id = type_id;
      return false;
    }

    auto iter = part_index.find(container.get());
    if (iter == part_index.end()) {
      part_index[container.get()] = parts.size();
      parts.emplace_back();
      parts.back().container = container;
      iter = part_index.find(container.get());
    }
    *parts[iter->second].requests.Add() = request;
    return true;
  });
  if (!group_ok) {
    return result;
  }

  for (auto& part : parts) {
    ItemOperationResult part_result = part.container->check_has(config_group, part.requests);
    if (part_result.error_code != PROJECT_NAMESPACE_ID::EN_SUCCESS) {
      return part_result;
    }
  }

  return result;
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
