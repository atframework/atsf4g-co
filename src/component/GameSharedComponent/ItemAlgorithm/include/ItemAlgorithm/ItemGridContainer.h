// Copyright 2025 atframework

#pragma once

#include "ItemAlgorithm/ItemAlgorithmConfig.h"
#include "ItemAlgorithm/ItemGridAlgorithm.h"
#include "ItemAlgorithm/ItemGridData.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace excel {
struct config_group_t;
}

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridContainerAddCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ITEM_ALGORITHM_API ItemGridContainerAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridAddRequest&& in_requests);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerAddCheckedRequest(const ItemGridContainerAddCheckedRequest&) = delete;
  ItemGridContainerAddCheckedRequest& operator=(const ItemGridContainerAddCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemGridContainerAddCheckedRequest(ItemGridContainerAddCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemGridContainerAddCheckedRequest& operator=(ItemGridContainerAddCheckedRequest&&) noexcept;

 private:
  ATFW_UTIL_SYMBOL_LOCAL ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridAddRequest requests;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridOperationResult result;
  struct PerGridData {
    ITEM_ALGORITHM_API PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridAddCheckedRequest&& in_add_requests,
                                   std::vector<int32_t>&& in_original_indices);
    ATFW_UTIL_SYMBOL_LOCAL item_grid_algorithm_ptr_t grid = nullptr;
    ATFW_UTIL_SYMBOL_LOCAL ItemGridAddCheckedRequest add_requests;
    ATFW_UTIL_SYMBOL_LOCAL std::vector<int32_t> original_indices;
  };
  ATFW_UTIL_SYMBOL_LOCAL std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  ATFW_UTIL_SYMBOL_LOCAL bool apply = false;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridContainerSubCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ITEM_ALGORITHM_API ItemGridContainerSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridSubRequest&& in_requests);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerSubCheckedRequest(const ItemGridContainerSubCheckedRequest&) = delete;
  ItemGridContainerSubCheckedRequest& operator=(const ItemGridContainerSubCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemGridContainerSubCheckedRequest(ItemGridContainerSubCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemGridContainerSubCheckedRequest& operator=(ItemGridContainerSubCheckedRequest&&) noexcept;

 private:
  ATFW_UTIL_SYMBOL_LOCAL ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridSubRequest requests;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridOperationResult result;

  struct PerGridData {
    ITEM_ALGORITHM_API PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridSubCheckedRequest&& in_sub_requests,
                                   std::vector<int32_t>&& in_original_indices);
    ATFW_UTIL_SYMBOL_LOCAL item_grid_algorithm_ptr_t grid = nullptr;
    ATFW_UTIL_SYMBOL_LOCAL ItemGridSubCheckedRequest sub_requests;
    ATFW_UTIL_SYMBOL_LOCAL std::vector<int32_t> original_indices;
  };
  ATFW_UTIL_SYMBOL_LOCAL std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  ATFW_UTIL_SYMBOL_LOCAL bool apply = false;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridContainerMoveRequest {
  PROJECT_NAMESPACE_ID::DItemBasic source_item_basic;
  PROJECT_NAMESPACE_ID::DItemPosition target_position;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridContainerMoveCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ITEM_ALGORITHM_API ItemGridContainerMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      std::vector<ItemGridContainerMoveRequest>&& in_requests);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerMoveCheckedRequest(const ItemGridContainerMoveCheckedRequest&) = delete;
  ItemGridContainerMoveCheckedRequest& operator=(const ItemGridContainerMoveCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemGridContainerMoveCheckedRequest(ItemGridContainerMoveCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemGridContainerMoveCheckedRequest& operator=(ItemGridContainerMoveCheckedRequest&&) noexcept;

 private:
  ATFW_UTIL_SYMBOL_LOCAL ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ATFW_UTIL_SYMBOL_LOCAL std::vector<ItemGridContainerMoveRequest> requests;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridOperationResult result;
  struct PerGridData {
    ITEM_ALGORITHM_API PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridMoveCheckedRequest&& in_move_request,
                                   std::vector<int32_t>&& in_original_indices);
    ATFW_UTIL_SYMBOL_LOCAL item_grid_algorithm_ptr_t grid = nullptr;
    ATFW_UTIL_SYMBOL_LOCAL ItemGridMoveCheckedRequest move_request;
    ATFW_UTIL_SYMBOL_LOCAL std::vector<int32_t> original_indices;
  };
  ATFW_UTIL_SYMBOL_LOCAL std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  ATFW_UTIL_SYMBOL_LOCAL bool apply = false;
};

struct ATFW_UTIL_SYMBOL_VISIBLE ItemGridContainerReplaceCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ITEM_ALGORITHM_API ItemGridContainerReplaceCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridReplaceRequest&& in_requests);
  ATFW_UTIL_FORCEINLINE int32_t get_error_code() const { return result.error_code; }
  ATFW_UTIL_FORCEINLINE int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerReplaceCheckedRequest(const ItemGridContainerReplaceCheckedRequest&) = delete;
  ItemGridContainerReplaceCheckedRequest& operator=(const ItemGridContainerReplaceCheckedRequest&) = delete;
  ITEM_ALGORITHM_API ItemGridContainerReplaceCheckedRequest(ItemGridContainerReplaceCheckedRequest&&) noexcept;
  ITEM_ALGORITHM_API ItemGridContainerReplaceCheckedRequest& operator=(ItemGridContainerReplaceCheckedRequest&&) noexcept;

 private:
  ATFW_UTIL_SYMBOL_LOCAL ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridReplaceRequest requests;
  ATFW_UTIL_SYMBOL_LOCAL ItemGridOperationResult result;
  struct PerGridData {
    ITEM_ALGORITHM_API PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridReplaceCheckedRequest&& in_replace_request,
                                   std::vector<int32_t>&& in_original_indices);
    ATFW_UTIL_SYMBOL_LOCAL item_grid_algorithm_ptr_t grid = nullptr;
    ATFW_UTIL_SYMBOL_LOCAL ItemGridReplaceCheckedRequest replace_requests;
    ATFW_UTIL_SYMBOL_LOCAL std::vector<int32_t> original_indices;
  };
  ATFW_UTIL_SYMBOL_LOCAL std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  ATFW_UTIL_SYMBOL_LOCAL bool apply = false;
};

class ATFW_UTIL_SYMBOL_VISIBLE ItemGridContainer {
 public:
  ITEM_ALGORITHM_API ItemGridContainer();
  ITEM_ALGORITHM_API virtual ~ItemGridContainer();

  ItemGridContainer(const ItemGridContainer&) = delete;
  ItemGridContainer& operator=(const ItemGridContainer&) = delete;
  ItemGridContainer(ItemGridContainer&&) = delete;
  ItemGridContainer& operator=(ItemGridContainer&&) = delete;

  // ============================================================
  // 路由虚函数 — 子类必须实现
  // ============================================================
  ITEM_ALGORITHM_API virtual item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) = 0;
  ITEM_ALGORITHM_API virtual item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) const = 0;

  // ============================================================
  // 批量操作接口
  // ============================================================
  ITEM_ALGORITHM_API ItemGridContainerAddCheckedRequest check_add(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridAddRequest&& in_requests) const;
  ITEM_ALGORITHM_API ItemGridOperationResult add(ItemGridContainerAddCheckedRequest& checked_request);
  ITEM_ALGORITHM_API ItemGridContainerSubCheckedRequest check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridSubRequest&& in_requests) const;
  ITEM_ALGORITHM_API ItemGridOperationResult sub(ItemGridContainerSubCheckedRequest& checked_request);
  ITEM_ALGORITHM_API ItemGridContainerMoveCheckedRequest check_move(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      std::vector<ItemGridContainerMoveRequest>&& in_requests) const;
  ITEM_ALGORITHM_API ItemGridOperationResult move(ItemGridContainerMoveCheckedRequest& checked_request);
  // 整体替换 (仅单个 Grid 原子, 跨 Grid 不保证原子性)
  ITEM_ALGORITHM_API ItemGridContainerReplaceCheckedRequest check_replace(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridReplaceRequest&& in_requests) const;
  ITEM_ALGORITHM_API ItemGridOperationResult replace(ItemGridContainerReplaceCheckedRequest& checked_request);
  ITEM_ALGORITHM_API ItemGridOperationResult check_has(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridHasRequest& requests) const;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
