// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>
#include <protocol/pbdesc/com.protocol.user.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>

#include <logic/item/user_item_grid_algorithm.h>
#include <logic/item/user_item_manager.h>
#include <logic/item/user_virtual_inventory.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpc {
class context;
}
class user;

class user_grid_item_operation_checked_add_data : public item_operation_checked_add_private_data {
 public:
  explicit user_grid_item_operation_checked_add_data(item_algorithm::ItemGridContainerAddCheckedRequest&& in_data)
      : data(std::move(in_data)) {}
  ~user_grid_item_operation_checked_add_data() override = default;

  user_grid_item_operation_checked_add_data(const user_grid_item_operation_checked_add_data&) = delete;
  user_grid_item_operation_checked_add_data& operator=(const user_grid_item_operation_checked_add_data&) = delete;
  user_grid_item_operation_checked_add_data(user_grid_item_operation_checked_add_data&&) = default;
  user_grid_item_operation_checked_add_data& operator=(user_grid_item_operation_checked_add_data&&) = default;

  item_algorithm::ItemGridContainerAddCheckedRequest data;
};

class user_grid_item_operation_checked_sub_data : public item_operation_checked_sub_private_data {
 public:
  explicit user_grid_item_operation_checked_sub_data(item_algorithm::ItemGridContainerSubCheckedRequest&& in_data)
      : data(std::move(in_data)) {}
  ~user_grid_item_operation_checked_sub_data() override = default;

  user_grid_item_operation_checked_sub_data(const user_grid_item_operation_checked_sub_data&) = delete;
  user_grid_item_operation_checked_sub_data& operator=(const user_grid_item_operation_checked_sub_data&) = delete;
  user_grid_item_operation_checked_sub_data(user_grid_item_operation_checked_sub_data&&) = default;
  user_grid_item_operation_checked_sub_data& operator=(user_grid_item_operation_checked_sub_data&&) = default;

  item_algorithm::ItemGridContainerSubCheckedRequest data;
};

class user_grid_item_operation_handler : public item_operation_handler {
 public:
  item_operation_handle_checked_add_request check_add(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&&) const override;
  item_operation_handle_checked_sub_request check_sub(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const override;
  item_operation_result check_has(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const override;

  item_operation_result add(rpc::context&, user&, item_operation_handle_checked_add_request&&) override;
  item_operation_result sub(rpc::context&, user&, item_operation_handle_checked_sub_request&&) override;

  bool find_position(rpc::context&, user&,
                     google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&) override;

  ~user_grid_item_operation_handler() override;
};

class user_item_grid_manager : public atfw::util::design_pattern::noncopyable,
                               public item_algorithm::ItemGridContainer {
 public:
  struct dirty_entry_hash {
    size_t operator()(const std::pair<int64_t, uint64_t>& p) const noexcept {
      size_t h = std::hash<int64_t>{}(p.first);
      h ^= std::hash<uint64_t>{}(p.second) + size_t{0x9e3779b9} + (h << 6) + (h >> 2);
      return h;
    }
  };
  explicit user_item_grid_manager(user& owner);

  user& get_owner() { return *owner_; }
  const user& get_owner() const { return *owner_; }

  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);
  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user_table) const;
  void dump_virtual_inventory(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const;
  void create_init(rpc::context& ctx);
  void login_init(rpc::context& ctx);

  using find_position_handle_t = std::function<bool(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&)>;
  static void register_find_position_handle(gsl::span<PROJECT_NAMESPACE_ID::EnItemType> item_type,
                                            find_position_handle_t handle);

 public:
  // 操作接口
  item_algorithm::ItemGridContainerAddCheckedRequest check_add(item_algorithm::ItemGridAddRequest&& in_requests) const;
  item_algorithm::ItemGridOperationResult add(item_algorithm::ItemGridContainerAddCheckedRequest& checked_request);
  item_algorithm::ItemGridContainerSubCheckedRequest check_sub(item_algorithm::ItemGridSubRequest&& in_requests) const;
  item_algorithm::ItemGridOperationResult sub(item_algorithm::ItemGridContainerSubCheckedRequest& checked_request);
  item_algorithm::ItemGridContainerMoveCheckedRequest check_move(
      std::vector<item_algorithm::ItemGridContainerMoveRequest>&& in_requests) const;
  item_algorithm::ItemGridOperationResult move(item_algorithm::ItemGridContainerMoveCheckedRequest& checked_request);
  item_algorithm::ItemGridContainerReplaceCheckedRequest check_replace(
      item_algorithm::ItemGridReplaceRequest&& in_requests) const;
  item_algorithm::ItemGridOperationResult replace(
      item_algorithm::ItemGridContainerReplaceCheckedRequest& checked_request);
  item_algorithm::ItemGridOperationResult check_has(const item_algorithm::ItemGridHasRequest& requests) const;
  bool find_position(rpc::context&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&) const;

 public:
  int64_t allocate_container_guid();
  user_virtual_inventory& get_virtual_inventory() { return virtual_inventory_; }
  const user_virtual_inventory& get_virtual_inventory() const { return virtual_inventory_; }
  int64_t get_virtual_inventory_container_guid() const {
    return virtual_inventory_.get_virtual_grid()->get_container_guid();
  }

  void register_item_grid_algorithm(atfw::util::memory::strong_rc_ptr<user_item_grid_algorithm> grid);
  void unregister_item_grid_algorithm(user_item_grid_algorithm* ATFW_UTIL_MACRO_NONNULL grid);
  atfw::util::memory::strong_rc_ptr<user_item_grid_algorithm> find_grid_by_container_guid(int64_t container_guid) const;

  void on_item_changed(int64_t container_guid, const item_algorithm::item_grid_entry_ptr_t& entry,
                       item_algorithm::ItemGridOperationReason reason);

  /// @brief 构建脏同步消息 (user dirty handle 的 build_fn 调用, 填充 SCUserDirtyChgSync)
  void build_dirty_sync(PROJECT_NAMESPACE_ID::SCUserDirtyChgSync& output);

 private:
  item_algorithm::item_grid_algorithm_ptr_t select_grid(const PROJECT_NAMESPACE_ID::DItemPosition& position) override;
  item_algorithm::item_grid_algorithm_ptr_t select_grid(
      const PROJECT_NAMESPACE_ID::DItemPosition& position) const override;

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  PROJECT_NAMESPACE_ID::DUserItemManagerData manager_data_;
  user_virtual_inventory virtual_inventory_;

  std::unordered_map<int64_t, atfw::util::memory::weak_rc_ptr<user_item_grid_algorithm>> container_guid_to_grid_;
  std::unordered_set<std::pair<int64_t, uint64_t>, dirty_entry_hash> dirty_entries_;

  static std::unordered_map<PROJECT_NAMESPACE_ID::EnItemType, int32_t> find_position_handle_id_;
  static std::unordered_map<int32_t, find_position_handle_t> find_position_handle;
};
