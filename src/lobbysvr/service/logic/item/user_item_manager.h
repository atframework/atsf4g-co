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

#include <memory/rc_ptr.h>
#include <utility>

namespace rpc {
class context;
}
class user;

struct item_operation_result {
  int32_t error_code = PROJECT_NAMESPACE_ID::EN_SUCCESS;
  // 操作失败时, 表示第几个请求失败(从0开始), -1表示整体失败
  int32_t failed_index = -1;
};

class item_operation_checked_add_private_data {
 public:
  virtual ~item_operation_checked_add_private_data() = default;
};
class item_operation_checked_sub_private_data {
 public:
  virtual ~item_operation_checked_sub_private_data() = default;
};

struct item_operation_checked_add_request {
  item_operation_result check_result;
  atfw::util::memory::strong_rc_ptr<item_operation_checked_add_private_data> checked_request = nullptr;
};
struct item_operation_checked_sub_request {
  item_operation_result check_result;
  atfw::util::memory::strong_rc_ptr<item_operation_checked_sub_private_data> checked_request = nullptr;
};

class item_operation_handler {
 public:
  virtual item_operation_checked_add_request check_add(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&&) const {
    return item_operation_checked_add_request{item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_NOT_IMPLEMENTED, -1},
                                              nullptr};
  }
  virtual item_operation_checked_sub_request check_sub(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const {
    return item_operation_checked_sub_request{item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_NOT_IMPLEMENTED, -1},
                                              nullptr};
  }
  virtual item_operation_result check_has(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const {
    return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_NOT_IMPLEMENTED, -1};
  }

  virtual item_operation_result add(rpc::context&, user&, item_operation_checked_add_request&&) {
    return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_NOT_IMPLEMENTED, -1};
  }
  virtual item_operation_result sub(rpc::context&, user&, item_operation_checked_sub_request&&) {
    return item_operation_result{PROJECT_NAMESPACE_ID::EN_ERR_NOT_IMPLEMENTED, -1};
  }
};

class user_item_manager : public atfw::util::design_pattern::noncopyable {
 public:
  explicit user_item_manager(user& owner);
  user& get_owner() { return *owner_; }
  const user& get_owner() const { return *owner_; }

  static void register_item_type_handler(PROJECT_NAMESPACE_ID::EnItemType item_type,
                                         atfw::util::memory::strong_rc_ptr<item_operation_handler> handler);

 public:
  item_operation_checked_add_request check_add(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&&) const;
  item_operation_checked_sub_request check_sub(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const;
  item_operation_result check_has(rpc::context&, user&,
                                  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&);

  item_operation_result add(rpc::context&, user&, item_operation_checked_add_request&&);
  item_operation_result sub(rpc::context&, user&, item_operation_checked_sub_request&&);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
};