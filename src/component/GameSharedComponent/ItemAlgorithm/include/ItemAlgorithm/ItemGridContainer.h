// Copyright 2025 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>
#include <ItemAlgorithm/ItemGridAlgorithm.h>
#include <ItemAlgorithm/ItemGridData.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace excel {
struct config_group_t;
}

ITEM_ALGORITHM_NAMESPACE_BEGIN

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251)
#endif

namespace item_algorithm {

struct ITEM_ALGORITHM_API ItemGridContainerAddCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ItemGridContainerAddCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridAddRequest&& in_requests)
      : config_group(in_config_group), requests(std::move(in_requests)) {}
  int32_t get_error_code() const { return result.error_code; }
  int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerAddCheckedRequest(const ItemGridContainerAddCheckedRequest&) = delete;
  ItemGridContainerAddCheckedRequest& operator=(const ItemGridContainerAddCheckedRequest&) = delete;
  ItemGridContainerAddCheckedRequest(ItemGridContainerAddCheckedRequest&&) = default;
  ItemGridContainerAddCheckedRequest& operator=(ItemGridContainerAddCheckedRequest&&) = default;

 private:
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridAddRequest requests;
  ItemGridOperationResult result;
  struct PerGridData {
    PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridAddCheckedRequest&& in_add_requests,
                std::vector<int32_t>&& in_original_indices)
        : grid(in_grid), add_requests(std::move(in_add_requests)), original_indices(std::move(in_original_indices)) {}
    item_grid_algorithm_ptr_t grid = nullptr;
    ItemGridAddCheckedRequest add_requests;
    std::vector<int32_t> original_indices;
  };
  std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  bool apply = false;
};

struct ITEM_ALGORITHM_API ItemGridContainerSubCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ItemGridContainerSubCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridSubRequest&& in_requests)
      : config_group(in_config_group), requests(std::move(in_requests)) {}
  int32_t get_error_code() const { return result.error_code; }
  int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerSubCheckedRequest(const ItemGridContainerSubCheckedRequest&) = delete;
  ItemGridContainerSubCheckedRequest& operator=(const ItemGridContainerSubCheckedRequest&) = delete;
  ItemGridContainerSubCheckedRequest(ItemGridContainerSubCheckedRequest&&) = default;
  ItemGridContainerSubCheckedRequest& operator=(ItemGridContainerSubCheckedRequest&&) = default;

 private:
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridSubRequest requests;
  ItemGridOperationResult result;

  struct PerGridData {
    PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridSubCheckedRequest&& in_sub_requests,
                std::vector<int32_t>&& in_original_indices)
        : grid(in_grid), sub_requests(std::move(in_sub_requests)), original_indices(std::move(in_original_indices)) {}
    item_grid_algorithm_ptr_t grid = nullptr;
    ItemGridSubCheckedRequest sub_requests;
    std::vector<int32_t> original_indices;
  };
  std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  bool apply = false;
};

struct ITEM_ALGORITHM_API ItemGridContainerMoveRequest {
  PROJECT_NAMESPACE_ID::DItemBasic source_item_basic;
  PROJECT_NAMESPACE_ID::DItemPosition target_position;
};

struct ITEM_ALGORITHM_API ItemGridContainerMoveCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ItemGridContainerMoveCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      std::vector<ItemGridContainerMoveRequest>&& in_requests)
      : config_group(in_config_group), requests(std::move(in_requests)) {}
  int32_t get_error_code() const { return result.error_code; }
  int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerMoveCheckedRequest(const ItemGridContainerMoveCheckedRequest&) = delete;
  ItemGridContainerMoveCheckedRequest& operator=(const ItemGridContainerMoveCheckedRequest&) = delete;
  ItemGridContainerMoveCheckedRequest(ItemGridContainerMoveCheckedRequest&&) = default;
  ItemGridContainerMoveCheckedRequest& operator=(ItemGridContainerMoveCheckedRequest&&) = default;

 private:
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  std::vector<ItemGridContainerMoveRequest> requests;
  ItemGridOperationResult result;
  struct PerGridData {
    PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridMoveCheckedRequest&& in_move_request,
                std::vector<int32_t>&& in_original_indices)
        : grid(in_grid), move_request(std::move(in_move_request)), original_indices(std::move(in_original_indices)) {}
    item_grid_algorithm_ptr_t grid = nullptr;
    ItemGridMoveCheckedRequest move_request;
    std::vector<int32_t> original_indices;
  };
  std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  bool apply = false;
};

struct ITEM_ALGORITHM_API ItemGridContainerReplaceCheckedRequest {
  friend class ItemGridContainer;
  friend class ItemGridAlgorithm;

  ItemGridContainerReplaceCheckedRequest(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridReplaceRequest&& in_requests)
      : config_group(in_config_group), requests(std::move(in_requests)) {}
  int32_t get_error_code() const { return result.error_code; }
  int32_t get_failed_index() const { return result.failed_index; }

  ItemGridContainerReplaceCheckedRequest(const ItemGridContainerReplaceCheckedRequest&) = delete;
  ItemGridContainerReplaceCheckedRequest& operator=(const ItemGridContainerReplaceCheckedRequest&) = delete;
  ItemGridContainerReplaceCheckedRequest(ItemGridContainerReplaceCheckedRequest&&) = default;
  ItemGridContainerReplaceCheckedRequest& operator=(ItemGridContainerReplaceCheckedRequest&&) = default;

 private:
  ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> config_group;
  ItemGridReplaceRequest requests;
  ItemGridOperationResult result;
  struct PerGridData {
    PerGridData(item_grid_algorithm_ptr_t in_grid, ItemGridReplaceCheckedRequest&& in_replace_request,
                std::vector<int32_t>&& in_original_indices)
        : grid(in_grid),
          replace_requests(std::move(in_replace_request)),
          original_indices(std::move(in_original_indices)) {}
    item_grid_algorithm_ptr_t grid = nullptr;
    ItemGridReplaceCheckedRequest replace_requests;
    std::vector<int32_t> original_indices;
  };
  std::vector<atfw::util::memory::strong_rc_ptr<PerGridData>> grid_data;
  bool apply = false;
};

class ITEM_ALGORITHM_API ItemGridContainer {
 public:
  ItemGridContainer();
  virtual ~ItemGridContainer();

  ItemGridContainer(const ItemGridContainer&) = delete;
  ItemGridContainer& operator=(const ItemGridContainer&) = delete;
  ItemGridContainer(ItemGridContainer&&) = delete;
  ItemGridContainer& operator=(ItemGridContainer&&) = delete;

  // ============================================================
  // 路由虚函数 — 子类必须实现
  // ============================================================
  virtual item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) = 0;
  virtual item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) const = 0;

  // ============================================================
  // 批量操作接口
  // ============================================================
  ItemGridContainerAddCheckedRequest check_add(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridAddRequest&& in_requests) const;
  ItemGridOperationResult add(ItemGridContainerAddCheckedRequest& checked_request);
  ItemGridContainerSubCheckedRequest check_sub(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridSubRequest&& in_requests) const;
  ItemGridOperationResult sub(ItemGridContainerSubCheckedRequest& checked_request);
  ItemGridContainerMoveCheckedRequest check_move(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      std::vector<ItemGridContainerMoveRequest>&& in_requests) const;
  ItemGridOperationResult move(ItemGridContainerMoveCheckedRequest& checked_request);
  // 整体替换 (仅单个 Grid 原子, 跨 Grid 不保证原子性)
  ItemGridContainerReplaceCheckedRequest check_replace(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& in_config_group,
      ItemGridReplaceRequest&& in_requests) const;
  ItemGridOperationResult replace(ItemGridContainerReplaceCheckedRequest& checked_request);
  ItemGridOperationResult check_has(
      const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t>& config_group,
      const ItemGridHasRequest& requests) const;
};

}  // namespace item_algorithm

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

ITEM_ALGORITHM_NAMESPACE_END
