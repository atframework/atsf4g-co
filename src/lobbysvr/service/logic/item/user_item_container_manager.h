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

#include <ItemAlgorithm/Container/ItemFiniteGridContainer.h>
#include <ItemAlgorithm/Container/ItemInfiniteGridContainer.h>
#include <ItemAlgorithm/Container/ItemNoPositionContainer.h>
#include <ItemAlgorithm/ItemContainer.h>
#include <ItemAlgorithm/ItemContainerGroup.h>
#include <logic/item/user_item_container.h>
#include <logic/item/user_item_manager.h>
#include <logic/item/user_item_operation_handler.h>
#include <logic/item/user_virtual_inventory.h>
#include <rpc/rpc_utils.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpc {
class context;
}
class user;

namespace item_algorithm = ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm;

/// @brief 组级 add checked request 的 handler 私有数据
class user_container_item_operation_checked_add_data : public item_operation_checked_add_private_data {
 public:
  explicit user_container_item_operation_checked_add_data(item_algorithm::ItemContainerGroupAddCheckedRequest&& in_data)
      : data(std::move(in_data)) {}
  ~user_container_item_operation_checked_add_data() override = default;

  user_container_item_operation_checked_add_data(const user_container_item_operation_checked_add_data&) = delete;
  user_container_item_operation_checked_add_data& operator=(const user_container_item_operation_checked_add_data&) =
      delete;
  user_container_item_operation_checked_add_data(user_container_item_operation_checked_add_data&&) = default;
  user_container_item_operation_checked_add_data& operator=(user_container_item_operation_checked_add_data&&) = default;

  item_algorithm::ItemContainerGroupAddCheckedRequest data;
};

/// @brief 组级 sub checked request 的 handler 私有数据
class user_container_item_operation_checked_sub_data : public item_operation_checked_sub_private_data {
 public:
  explicit user_container_item_operation_checked_sub_data(item_algorithm::ItemContainerGroupSubCheckedRequest&& in_data)
      : data(std::move(in_data)) {}
  ~user_container_item_operation_checked_sub_data() override = default;

  user_container_item_operation_checked_sub_data(const user_container_item_operation_checked_sub_data&) = delete;
  user_container_item_operation_checked_sub_data& operator=(const user_container_item_operation_checked_sub_data&) =
      delete;
  user_container_item_operation_checked_sub_data(user_container_item_operation_checked_sub_data&&) = default;
  user_container_item_operation_checked_sub_data& operator=(user_container_item_operation_checked_sub_data&&) = default;

  item_algorithm::ItemContainerGroupSubCheckedRequest data;
};

/// @brief 容器类道具 (货币 / 虚拟道具 / 普通道具 / 装备) 的操作处理器
///
/// 只做协议层与容器组之间的适配: 入参包装成迭代器视图、把组回报的 failed_type_id 映射回
/// 请求下标 (上层接口要的是下标), 真正的路由 / 分片 / 校验 / 执行都在 user_item_container_manager
/// (即 SDK 的 ItemContainerGroup) 里。
class user_container_item_operation_handler : public item_operation_handler {
 public:
  item_operation_handle_checked_add_request check_add(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&&) const override;
  item_operation_handle_checked_sub_request check_sub(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const override;
  item_operation_result check_has(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const override;

  item_operation_result add(rpc::context&, user&, item_operation_handle_checked_add_request&&) override;
  item_operation_result sub(rpc::context&, user&, item_operation_handle_checked_sub_request&&) override;

  int32_t on_item_not_enough(rpc::context&, user&, int32_t type_id) const override;
  int64_t get_count(rpc::context&, user&, int32_t type_id) const override;

  bool find_position(rpc::context&, user&,
                     google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&) override;

  ~user_container_item_operation_handler() override;
};

/// @brief 用户的物品容器管理器
///
/// 本身就是 SDK 的容器组 (ItemContainerGroup):
///   - select_container 按 position.container_guid 在登记表里选容器;
///   - check_add / add / check_sub / sub / check_move / move / check_has
///     都由组实现 (按 position 分片、逐容器校验与执行、第一个失败即返回);
///   - 本层只补上配置组 (excel::get_current_config_group) 与操作来源, 以及容器登记、脏数据同步。
class user_item_container_manager : public atfw::util::design_pattern::noncopyable,
                                   public item_algorithm::ItemContainerGroup {
 public:
  struct dirty_entry_hash {
    size_t operator()(const std::pair<int64_t, uint64_t>& p) const noexcept {
      size_t h = std::hash<int64_t>{}(p.first);
      h ^= std::hash<uint64_t>{}(p.second) + size_t{0x9e3779b9} + (h << 6) + (h >> 2);
      return h;
    }
  };
  explicit user_item_container_manager(user& owner);

  user& get_owner() { return *owner_; }
  const user& get_owner() const { return *owner_; }

  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);
  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user_table) const;
  void dump_virtual_inventory(PROJECT_NAMESPACE_ID::DUserVirtualInventoryData& out) const;
  void dump_virtual_inventory(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out) const;

  rpc::result_void_type create_init(rpc::context& ctx);
  void login_init(rpc::context& ctx);

  using find_position_handle_t = std::function<bool(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&)>;
  static void register_find_position_handle(gsl::span<const PROJECT_NAMESPACE_ID::EnItemType> item_type,
                                            find_position_handle_t handle);

 public:
  // ---- 组级操作 ----
  // 组自己按 position 分片, 这里只补上配置组与操作来源; 多物品接口收迭代器视图,
  // move 收完整的移动请求列表 (组内部按源 / 目标容器拆成 sub + add)。
  item_algorithm::ItemContainerGroupAddCheckedRequest check_add(
      const item_algorithm::item_instance_readable_iterable& requests,
      const item_algorithm::ItemOperationSource& source = item_algorithm::ItemOperationSource{}) const;
  item_algorithm::ItemOperationResult add(item_algorithm::ItemContainerGroupAddCheckedRequest& checked_request);

  item_algorithm::ItemContainerGroupSubCheckedRequest check_sub(
      const item_algorithm::item_basic_readable_iterable& requests,
      const item_algorithm::ItemOperationSource& source = item_algorithm::ItemOperationSource{}) const;
  item_algorithm::ItemOperationResult sub(item_algorithm::ItemContainerGroupSubCheckedRequest& checked_request);

  item_algorithm::ItemContainerGroupMoveCheckedRequest check_move(
      std::vector<item_algorithm::ItemContainerGroupMoveRequest>&& requests,
      const item_algorithm::ItemOperationSource& source = item_algorithm::ItemOperationSource{}) const;
  item_algorithm::ItemOperationResult move(item_algorithm::ItemContainerGroupMoveCheckedRequest& checked_request);

  item_algorithm::ItemOperationResult check_has(const item_algorithm::item_basic_readable_iterable& requests) const;

  bool find_position(rpc::context&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&) const;
  int32_t get_item_not_enough_error_code(rpc::context&, int32_t type_id) const;
  int64_t get_count(rpc::context&, int32_t type_id) const;

 public:
  int64_t allocate_container_guid();
  user_virtual_inventory& get_virtual_inventory() { return virtual_inventory_; }
  const user_virtual_inventory& get_virtual_inventory() const { return virtual_inventory_; }
  int64_t get_virtual_inventory_container_guid() const {
    return virtual_inventory_.get_virtual_container()->get_container_guid();
  }

  void register_item_container(item_algorithm::item_container_ptr_t container);
  void unregister_item_container(item_algorithm::ItemContainer* ATFW_UTIL_MACRO_NONNULL container);
  item_algorithm::item_container_ptr_t find_item_container(int64_t container_guid) const;

  void on_item_changed(int64_t container_guid, const item_algorithm::item_entry_ptr_t& entry,
                       const item_algorithm::ItemOperationContext& context);
  void on_item_count_changed(int32_t type_id, int64_t delta_count);

  /// @brief 构建脏同步消息 (user dirty handle 的 build_fn 调用, 填充 SCUserDirtyChgSync)
  void build_dirty_sync(PROJECT_NAMESPACE_ID::SCUserDirtyChgSync& output);

 private:
  // ---- 组的路由 (按 position.container_guid 在登记表里选容器) ----
  item_algorithm::item_container_ptr_t select_container(const PROJECT_NAMESPACE_ID::DItemPosition& position) override;
  item_algorithm::item_container_ptr_t select_container(
      const PROJECT_NAMESPACE_ID::DItemPosition& position) const override;

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  PROJECT_NAMESPACE_ID::DUserItemManagerData manager_data_;
  user_virtual_inventory virtual_inventory_;

  std::unordered_map<int32_t, int64_t> item_type_to_count_cache_;

  // 只保留弱引用: 容器的生命周期由持有它的业务对象决定 (测试用例也自己持有)
  std::unordered_map<int64_t, atfw::util::memory::weak_rc_ptr<item_algorithm::ItemContainer>>
      container_guid_to_container_;
  std::unordered_set<std::pair<int64_t, uint64_t>, dirty_entry_hash> dirty_entries_;

  static std::unordered_map<PROJECT_NAMESPACE_ID::EnItemType, int32_t> find_position_handle_id_;
  static std::unordered_map<int32_t, find_position_handle_t> find_position_handle;
};
