// Copyright 2025 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>

#include <cstdint>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

class ItemContainer;

/// @brief 容器内的一个物品条目
///
/// 条目持有 DItemInstance 与所属容器, 析构时从所属容器的 entry_id 索引中摘除自己。
/// 条目本身不关心容器是哪种模式, 位置/占用语义由容器决定。
struct ATFW_UTIL_SYMBOL_VISIBLE ItemEntry : public atfw::util::memory::enable_shared_rc_from_this<ItemEntry> {
  ITEM_ALGORITHM_API explicit ItemEntry(atfw::util::memory::strong_rc_ptr<ItemContainer> in_belong_container,
                                        PROJECT_NAMESPACE_ID::DItemInstance&& inst, uint64_t in_entry_id);
  ITEM_ALGORITHM_API ~ItemEntry();
  ITEM_ALGORITHM_API uint64_t entry_id() const;
  ITEM_ALGORITHM_API const PROJECT_NAMESPACE_ID::DItemInstance& item_instance() const;
  ITEM_ALGORITHM_API PROJECT_NAMESPACE_ID::DItemData& mutable_item_data();
  ITEM_ALGORITHM_API void mark_item_data_dirty();
  /// @brief 所属容器写入的排序键
  ///
  /// 对条目本身是不透明的: 条目只负责保存它, 不理解它的含义。
  /// 具体含义 (坐标 / 槽位 / …… ) 由容器按自己的位置语义决定。
  ITEM_ALGORITHM_API uint64_t sort_key() const;

 private:
  friend class ItemContainer;
  PROJECT_NAMESPACE_ID::DItemBasic& mutable_item_basic();
  PROJECT_NAMESPACE_ID::DItemInstance& mutable_item_instance();
  /// @brief 由所属容器写入排序键
  ITEM_ALGORITHM_API void set_sort_key(uint64_t key);

 private:
  uint64_t entry_id_ = 0;
  uint64_t sort_key_ = 0;
  PROJECT_NAMESPACE_ID::DItemInstance item_instance_;
  atfw::util::memory::weak_rc_ptr<ItemContainer> belong_container_;
};

using item_entry_ptr_t = atfw::util::memory::strong_rc_ptr<ItemEntry>;
using item_entry_weak_ptr_t = atfw::util::memory::weak_rc_ptr<ItemEntry>;

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
