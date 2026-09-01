// Copyright 2026 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>

#include <list>

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

class user_item_manager;

struct item_operation_handle_checked_add_request {
  item_operation_result check_result;
  atfw::util::memory::strong_rc_ptr<item_operation_checked_add_private_data> checked_request;
  item_operation_handle_checked_add_request(int32_t error_code, int32_t failed_index = -1)
      : check_result{error_code, failed_index} {}
  item_operation_handle_checked_add_request(
      atfw::util::memory::strong_rc_ptr<item_operation_checked_add_private_data> checked_request)
      : checked_request(std::move(checked_request)) {}

  item_operation_handle_checked_add_request(const item_operation_handle_checked_add_request&) = delete;
  item_operation_handle_checked_add_request& operator=(const item_operation_handle_checked_add_request&) = delete;
  item_operation_handle_checked_add_request(item_operation_handle_checked_add_request&&) = default;
  item_operation_handle_checked_add_request& operator=(item_operation_handle_checked_add_request&&) = default;
};
struct item_operation_handle_checked_sub_request {
  item_operation_result check_result;
  atfw::util::memory::strong_rc_ptr<item_operation_checked_sub_private_data> checked_request;
  item_operation_handle_checked_sub_request(int32_t error_code, int32_t failed_index = -1)
      : check_result{error_code, failed_index} {}
  item_operation_handle_checked_sub_request(
      atfw::util::memory::strong_rc_ptr<item_operation_checked_sub_private_data> checked_request)
      : checked_request(std::move(checked_request)) {}

  item_operation_handle_checked_sub_request(const item_operation_handle_checked_sub_request&) = delete;
  item_operation_handle_checked_sub_request& operator=(const item_operation_handle_checked_sub_request&) = delete;
  item_operation_handle_checked_sub_request(item_operation_handle_checked_sub_request&&) = default;
  item_operation_handle_checked_sub_request& operator=(item_operation_handle_checked_sub_request&&) = default;
};

struct item_operation_checked_add_request {
  friend class user_item_manager;
  item_operation_result do_operation(rpc::context& ctx);

  item_operation_checked_add_request(const item_operation_checked_add_request&) = delete;
  item_operation_checked_add_request& operator=(const item_operation_checked_add_request&) = delete;
  item_operation_checked_add_request(item_operation_checked_add_request&&) = default;
  item_operation_checked_add_request& operator=(item_operation_checked_add_request&&) = default;

 private:
  item_operation_checked_add_request(user* ATFW_UTIL_MACRO_NONNULL owner,
                                     int32_t error_code = PROJECT_NAMESPACE_ID::EN_SUCCESS, int32_t failed_index = -1)
      : owner_(owner), check_result{error_code, failed_index} {}
  item_operation_checked_add_request(
      user* ATFW_UTIL_MACRO_NONNULL owner,
      std::list<std::pair<int32_t, item_operation_handle_checked_add_request>>&& checked_request)
      : owner_(owner), checked_request(std::move(checked_request)) {}

  user* ATFW_UTIL_MACRO_NONNULL owner_;
  item_operation_result check_result;
  std::list<std::pair<int32_t, item_operation_handle_checked_add_request>> checked_request;
};
struct item_operation_checked_sub_request {
  friend class user_item_manager;
  item_operation_result do_operation(rpc::context& ctx);

  item_operation_checked_sub_request(const item_operation_checked_sub_request&) = delete;
  item_operation_checked_sub_request& operator=(const item_operation_checked_sub_request&) = delete;
  item_operation_checked_sub_request(item_operation_checked_sub_request&&) = default;
  item_operation_checked_sub_request& operator=(item_operation_checked_sub_request&&) = default;

 private:
  item_operation_checked_sub_request(user* ATFW_UTIL_MACRO_NONNULL owner,
                                     int32_t error_code = PROJECT_NAMESPACE_ID::EN_SUCCESS, int32_t failed_index = -1)
      : owner_(owner), check_result{error_code, failed_index} {}
  item_operation_checked_sub_request(
      user* ATFW_UTIL_MACRO_NONNULL owner,
      std::list<std::pair<int32_t, item_operation_handle_checked_sub_request>>&& checked_request)
      : owner_(owner), checked_request(std::move(checked_request)) {}

  user* ATFW_UTIL_MACRO_NONNULL owner_;
  item_operation_result check_result;
  std::list<std::pair<int32_t, item_operation_handle_checked_sub_request>> checked_request;
};

class item_operation_handler {
 public:
  virtual item_operation_handle_checked_add_request check_add(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>&&) const = 0;
  virtual item_operation_handle_checked_sub_request check_sub(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const = 0;
  virtual item_operation_result check_has(
      rpc::context&, user&, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>&&) const = 0;

  virtual item_operation_result add(rpc::context&, user&, item_operation_handle_checked_add_request&&) = 0;
  virtual item_operation_result sub(rpc::context&, user&, item_operation_handle_checked_sub_request&&) = 0;
};