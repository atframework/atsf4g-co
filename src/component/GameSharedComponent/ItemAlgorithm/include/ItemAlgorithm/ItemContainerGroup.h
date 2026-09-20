// Copyright 2026 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>
#include <ItemAlgorithm/ItemContainer.h>
#include <ItemAlgorithm/ItemContainerTypes.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 组级请求容器 — 自持数据, 同时自身就是只读视图
//
// 容器层的 checked request 只引用传入的视图 (不复制数据), 所以组级分片必须自己持有数据与
// 具名视图: 这个类型把数据放在自己身上、自己就是视图, 生命周期天然覆盖执行阶段, 分片放进
// vector 也不会让视图悬空 (视图指向的是这个对象自己)。
// ============================================================

template <class T>
class item_container_group_request_list final : public item_iterable<T, false> {
 public:
  using base_type = item_iterable<T, false>;
  using callback_parameter = typename base_type::callback_parameter;
  using storage_type = ::google::protobuf::RepeatedPtrField<T>;

  item_container_group_request_list() = default;
  item_container_group_request_list(const item_container_group_request_list&) = default;
  item_container_group_request_list& operator=(const item_container_group_request_list&) = default;

  T* Add() { return storage_.Add(); }
  void Clear() { storage_.Clear(); }
  ATFW_UTIL_FORCEINLINE const storage_type& storage() const { return storage_; }

  bool empty() const noexcept override { return storage_.empty(); }
  size_t size() const noexcept override { return static_cast<size_t>(storage_.size()); }

  bool foreach(atfw::util::nostd::function_ref<bool(callback_parameter)> callback) const override {
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

/// @brief 组级移动请求: 把 source_item_basic 代表的道具挪到 target_position
///
/// source_item_basic 用 type_id + position 定位条目 (count 表示移动数量), target_position 是
/// 目标位置; 两个位置可以落在不同容器上 (跨容器移动由本层拆成 源容器扣减 + 目标容器放入)。
struct ATFW_UTIL_SYMBOL_VISIBLE ItemContainerGroupMoveRequest {
  PROJECT_NAMESPACE_ID::DItemBasic source_item_basic;
  PROJECT_NAMESPACE_ID::DItemPosition target_position;
};

/// @brief 组级 add checked request (按 position 拆给各容器后的结果)
///
/// check 阶段任一容器失败即整体失败, 不产生任何变更; 执行阶段逐容器 add, 第一个失败的容器
/// 结果直接返回 (error_code + failed_type_id), 不再回溯到组请求下标。
struct ATFW_UTIL_SYMBOL_VISIBLE ItemContainerGroupAddCheckedRequest {
  friend class ItemContainerGroup;

  ITEM_ALGORITHM_API explicit ItemContainerGroupAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_type_id() const { return result.failed_type_id; }
  ATFW_UTIL_FORCEINLINE const ItemOperationResult& get_result() const { return result; }

  ItemContainerGroupAddCheckedRequest(const ItemContainerGroupAddCheckedRequest&) = delete;
  ItemContainerGroupAddCheckedRequest& operator=(const ItemContainerGroupAddCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest(ItemContainerGroupAddCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest& operator=(ItemContainerGroupAddCheckedRequest&&) noexcept;

 private:
  /// @brief 单个容器的分片: 请求数据 + 容器层 checked request
  struct PerContainerData {
    ITEM_ALGORITHM_API explicit PerContainerData(item_container_ptr_t in_container);

    item_container_ptr_t container;
    /// 本分片的请求数据 (自己就是视图, 容器层 checked request 引用它)
    item_container_group_request_list<PROJECT_NAMESPACE_ID::DItemInstance> requests;
    /// 容器层 checked request (check 通过后填充)
    std::unique_ptr<ItemAddCheckedRequest> checked;
  };

  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemOperationResult result;
  std::vector<atfw::util::memory::strong_rc_ptr<PerContainerData>> container_data;
  bool apply = false;
};

/// @brief 组级 sub checked request
struct ATFW_UTIL_SYMBOL_VISIBLE ItemContainerGroupSubCheckedRequest {
  friend class ItemContainerGroup;

  ITEM_ALGORITHM_API explicit ItemContainerGroupSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_type_id() const { return result.failed_type_id; }
  ATFW_UTIL_FORCEINLINE const ItemOperationResult& get_result() const { return result; }

  ItemContainerGroupSubCheckedRequest(const ItemContainerGroupSubCheckedRequest&) = delete;
  ItemContainerGroupSubCheckedRequest& operator=(const ItemContainerGroupSubCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest(ItemContainerGroupSubCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest& operator=(ItemContainerGroupSubCheckedRequest&&) noexcept;

 private:
  struct PerContainerData {
    ITEM_ALGORITHM_API explicit PerContainerData(item_container_ptr_t in_container);

    item_container_ptr_t container;
    item_container_group_request_list<PROJECT_NAMESPACE_ID::DItemBasic> requests;
    std::unique_ptr<ItemSubCheckedRequest> checked;
  };

  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemOperationResult result;
  std::vector<atfw::util::memory::strong_rc_ptr<PerContainerData>> container_data;
  bool apply = false;
};

/// @brief 组级 move checked request (跨容器移动会拆成两个分片: 源容器 sub + 目标容器 add)
struct ATFW_UTIL_SYMBOL_VISIBLE ItemContainerGroupMoveCheckedRequest {
  friend class ItemContainerGroup;

  ITEM_ALGORITHM_API explicit ItemContainerGroupMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      std::vector<ItemContainerGroupMoveRequest>&& in_requests);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_type_id() const { return result.failed_type_id; }
  ATFW_UTIL_FORCEINLINE const ItemOperationResult& get_result() const { return result; }

  ItemContainerGroupMoveCheckedRequest(const ItemContainerGroupMoveCheckedRequest&) = delete;
  ItemContainerGroupMoveCheckedRequest& operator=(const ItemContainerGroupMoveCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest(ItemContainerGroupMoveCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest& operator=(ItemContainerGroupMoveCheckedRequest&&) noexcept;

 private:
  struct PerContainerData {
    ITEM_ALGORITHM_API explicit PerContainerData(item_container_ptr_t in_container);

    item_container_ptr_t container;
    /// 容器层 move checked request (move 请求按值自持, 不需要额外的数据存储)
    std::unique_ptr<ItemMoveCheckedRequest> checked;
  };

  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  /// 组级移动请求 (自持; 分片的 ItemMoveRequest 在 check 阶段按它构造)
  std::vector<ItemContainerGroupMoveRequest> requests;
  ItemOperationResult result;
  std::vector<atfw::util::memory::strong_rc_ptr<PerContainerData>> container_data;
  bool apply = false;
};

// ============================================================
// 容器组 — 按 DItemPosition 路由, 把一次批量请求拆给多个容器
//
// 这一层只做编排:
//   - select_container 由接入层实现 (按 position 找到目标容器);
//   - 请求按 position 分片, 每片交给对应容器的 check_* / add / sub / move;
//   - check 阶段任一容器失败即整体失败 (不产生变更), 执行阶段按分片顺序执行, 第一个失败即返回。
//
// 组级请求只接迭代器视图 (与容器层一致); 分片数据由 checked request 自己持有, 因此
// check_* 返回后调用方不再需要保证入参存活。
// ============================================================

class ATFW_UTIL_SYMBOL_VISIBLE ItemContainerGroup {
 public:
  ITEM_ALGORITHM_API ItemContainerGroup();
  ITEM_ALGORITHM_API virtual ~ItemContainerGroup();

  ItemContainerGroup(const ItemContainerGroup&) = delete;
  ItemContainerGroup& operator=(const ItemContainerGroup&) = delete;
  ItemContainerGroup(ItemContainerGroup&&) = delete;
  ItemContainerGroup& operator=(ItemContainerGroup&&) = delete;

  // ---- 路由虚函数 (接入层实现: 按 DItemPosition 选择目标容器) ----
  ITEM_ALGORITHM_API virtual item_container_ptr_t select_container(
      const PROJECT_NAMESPACE_ID::DItemPosition& position) = 0;
  ITEM_ALGORITHM_API virtual item_container_ptr_t select_container(
      const PROJECT_NAMESPACE_ID::DItemPosition& position) const = 0;

  // ---- 批量操作 ----
  ITEM_ALGORITHM_API ItemContainerGroupAddCheckedRequest
  check_add(const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& requests,
            const ItemOperationSource& source = ItemOperationSource{}) const;
  ITEM_ALGORITHM_API ItemOperationResult add(ItemContainerGroupAddCheckedRequest& checked_request);

  ITEM_ALGORITHM_API ItemContainerGroupSubCheckedRequest
  check_sub(const excel_config_group_ptr_t& config_group, const item_basic_readable_iterable& requests,
            const ItemOperationSource& source = ItemOperationSource{}) const;
  ITEM_ALGORITHM_API ItemOperationResult sub(ItemContainerGroupSubCheckedRequest& checked_request);

  ITEM_ALGORITHM_API ItemContainerGroupMoveCheckedRequest
  check_move(const excel_config_group_ptr_t& config_group, std::vector<ItemContainerGroupMoveRequest>&& requests,
             const ItemOperationSource& source = ItemOperationSource{}) const;
  ITEM_ALGORITHM_API ItemOperationResult move(ItemContainerGroupMoveCheckedRequest& checked_request);

  ITEM_ALGORITHM_API ItemOperationResult check_has(const excel_config_group_ptr_t& config_group,
                                                   const item_basic_readable_iterable& requests) const;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
