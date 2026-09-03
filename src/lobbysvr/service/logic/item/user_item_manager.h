// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>
#include <logic/item/user_item_operation_handler.h>
#include <memory/rc_ptr.h>
#include <rpc/rpc_utils.h>

#include <unordered_map>
#include <utility>
namespace rpc {
class context;
}
class user;

class user_item_manager : public atfw::util::design_pattern::noncopyable {
  friend struct item_operation_checked_add_request;
  friend struct item_operation_checked_sub_request;

 public:
  explicit user_item_manager(user& owner);
  user& get_owner() { return *owner_; }
  const user& get_owner() const { return *owner_; }

  static void register_item_type_handler(gsl::span<const PROJECT_NAMESPACE_ID::EnItemType> item_type,
                                         atfw::util::memory::strong_rc_ptr<item_operation_handler> handler);

 public:
  item_operation_checked_add_request check_add(
      rpc::context&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&&) const;
  item_operation_checked_sub_request check_sub(
      rpc::context&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const;
  // 只支持虚拟仓库道具
  item_operation_checked_sub_request check_sub(
      rpc::context&, const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>&,
      int32_t multiple = 1) const;
  item_operation_result check_has(rpc::context&,
                                  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&);

 public:
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type generate_item_from_offset_cfg(
      rpc::context&, const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>& offset_cfg,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& out_instances,
      int32_t multiple = 1) const;

  // Return False 时数据不可用 不要读取
  bool find_position(rpc::context&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& input);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::rpc_result<int64_t> generate_item_guid(rpc::context& ctx) const;

  static bool check_offset_instance_match(
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemScopeOffset>& offset_cfg,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& instances, int32_t multiple = 1);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;

  static std::unordered_map<PROJECT_NAMESPACE_ID::EnItemType, int32_t> item_type_handler_id_;
  static std::unordered_map<int32_t, atfw::util::memory::strong_rc_ptr<item_operation_handler>> item_type_handler_;
};
