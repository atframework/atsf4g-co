// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/extension/atframework.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <common/string_oprs.h>
#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

#include <config/logic_config.h>

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_action_base.h"
#include "rpc/rpc_common_types.h"
#include "utility/protobuf_mini_dumper.h"

namespace atapp {
class etcd_discovery_node;
}  // namespace atapp

class router_manager_base;
class router_object_base;

class ATFW_UTIL_SYMBOL_VISIBLE task_action_ss_req_base : public task_action_req_base<atframework::SSMsg> {
 public:
  using base_type = task_action_req_base<atframework::SSMsg>;
  using message_type = base_type::message_type;
  using result_type = base_type::result_type;
  using msg_ref_type = message_type &;
  using msg_cref_type = const message_type &;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  SERVER_FRAME_API explicit task_action_ss_req_base(dispatcher_start_data_type &&start_param);
  SERVER_FRAME_API virtual ~task_action_ss_req_base();

  SERVER_FRAME_API result_type hook_run() override;

  SERVER_FRAME_API uint64_t get_request_node_id() const noexcept;

  SERVER_FRAME_API const std::string &get_request_node_name() const noexcept;

  SERVER_FRAME_API static int32_t init_msg(msg_ref_type msg, uint64_t dst_pd, gsl::string_view node_name);
  SERVER_FRAME_API static int32_t init_msg(msg_ref_type msg, uint64_t dst_pd, gsl::string_view node_name,
                                           msg_cref_type req_msg);

  SERVER_FRAME_API std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  SERVER_FRAME_API const char *get_type_name() const override;

  SERVER_FRAME_API static void add_prepare_handle(rpc::result_code_type (*fn)(rpc::context &,
                                                                              task_action_ss_req_base &));

  SERVER_FRAME_API rpc::context::inherit_options get_inherit_option() const noexcept override;
  SERVER_FRAME_API rpc::context::trace_start_option get_trace_option() const noexcept override;

  SERVER_FRAME_API virtual bool is_stream_rpc() const noexcept;

  virtual bool unpack_request() noexcept = 0;

  virtual const std::string &get_request_type_url() const noexcept = 0;

  virtual const std::string &get_response_type_url() const noexcept = 0;

  /**
   * @brief Forward RPC to another server node
   *
   * @param node target node
   * @param transparent transparent forward
   * @param ok If forward success, if ok returns true, task should exit with return code
   * @param ignore_discovery ignore discovery
   * @return EXPLICIT_NODISCARD_ATTR
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type forward_rpc(const atapp::etcd_discovery_node &node,
                                                                             bool transparent, bool &ok,
                                                                             bool ignore_discovery = false);

  /**
   * @brief Forward RPC to another server node
   *
   * @param node_id target node
   * @param transparent transparent forward
   * @param ok If forward success, if ok returns true, task should exit with return code
   * @param ignore_discovery ignore discovery
   * @return EXPLICIT_NODISCARD_ATTR
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type forward_rpc(uint64_t node_id, bool transparent,
                                                                             bool &ok, bool ignore_discovery = false);

  /**
   * @brief Clone RPC to another server node
   *
   * @param node target node
   * @param response_message receive response message, set nullptr to ignore response
   * @param ignore_discovery ignore discovery
   * @return EXPLICIT_NODISCARD_ATTR
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type clone_rpc(const atapp::etcd_discovery_node &node,
                                                                           atframework::SSMsg *response_message,
                                                                           bool ignore_discovery = false);

  /**
   * @brief Clone RPC to another server node
   *
   * @param node_id target node
   * @param response_message receive response message, set nullptr to ignore response
   * @param ignore_discovery ignore discovery
   * @return EXPLICIT_NODISCARD_ATTR
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type clone_rpc(uint64_t node_id,
                                                                           atframework::SSMsg *response_message,
                                                                           bool ignore_discovery = false);

 protected:
  SERVER_FRAME_API msg_ref_type add_response_message();
  SERVER_FRAME_API void send_response() override;

  SERVER_FRAME_API virtual bool is_router_offline_ignored() const;  // 忽略路由对象不在线

  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type filter_router_msg(
      router_manager_base *&mgr, std::shared_ptr<router_object_base> &obj, std::pair<bool, int> &filter_result);

  ATFW_UTIL_FORCEINLINE bool has_response_message() const noexcept { return !response_messages_.empty(); }

 private:
  std::list<message_type *> response_messages_;
  static std::list<rpc::result_code_type (*)(rpc::context &, task_action_ss_req_base &)> prepare_handles_;
};

template <class TReqType, class TRspType>
class ATFW_UTIL_SYMBOL_VISIBLE task_action_ss_rpc_base : public task_action_ss_req_base {
 public:
  using base_type = task_action_ss_req_base;
  using message_type = base_type::message_type;
  using result_type = base_type::result_type;
  using msg_ref_type = message_type &;
  using msg_cref_type = const message_type &;
  using rpc_request_type = TReqType;
  using rpc_response_type = TRspType;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  explicit task_action_ss_rpc_base(dispatcher_start_data_type &&start_param)
      : base_type(std::move(start_param)),
        has_unpack_request_(false),
        has_pack_response_(false),
        request_body_(nullptr),
        response_body_(nullptr) {}

  rpc_request_type &get_request_body() {
    // Unpack on first call to get_request_body(), so router transfer need not unpack message body
    if (!has_unpack_request_) {
      unpack_request();
    }

    if (nullptr == request_body_) {
      static rpc_request_type empty_msg;
      empty_msg.Clear();
      return empty_msg;
    }

    return *request_body_;
  }

  rpc_response_type &get_response_body() {
    if (nullptr == response_body_) {
      response_body_ = get_shared_context().template create<rpc_response_type>();
    }

    if (nullptr == response_body_) {
      static rpc_response_type empty_msg;
      empty_msg.Clear();
      return empty_msg;
    }

    return *response_body_;
  }

  const std::string &get_request_type_url() const noexcept override {
    return rpc_request_type::descriptor()->full_name();
  }

  const std::string &get_response_type_url() const noexcept override {
    return rpc_response_type::descriptor()->full_name();
  }

 protected:
  void send_response() override {
    if (!has_pack_response_ && !is_stream_rpc() && !has_response_message() && is_response_message_enabled()) {
      pack_response();
    }
    base_type::send_response();
  }

 private:
  bool unpack_request() noexcept override {
    if (has_unpack_request_) {
      return true;
    }

    if (nullptr == request_body_) {
      request_body_ = get_shared_context().template create<rpc_request_type>();
    }
    if (nullptr == request_body_) {
      return false;
    }

    // Check message type
    if (get_request().head().has_rpc_request()) {
      if (get_request_type_url() != get_request().head().rpc_request().type_url()) {
        FCTXLOGERROR(get_shared_context(), "Except message {}, real got {}", get_request_type_url(),
                     get_request().head().rpc_request().type_url());
        return false;
      }
    } else if (get_request().head().has_rpc_stream()) {
      if (get_request_type_url() != get_request().head().rpc_stream().type_url()) {
        FCTXLOGERROR(get_shared_context(), "Except message {}, real got {}", get_request_type_url(),
                     get_request().head().rpc_stream().type_url());
        return false;
      }
    }

    if (false == request_body_->ParseFromString(get_request().body_bin())) {
      FCTXLOGERROR(get_shared_context(), "Try to parse message {} failed, msg: {}", get_request_type_url(),
                   request_body_->InitializationErrorString());
      return false;
    } else {
      FCTXLOGDEBUG(get_shared_context(), "Parse rpc request message {} success:\n{}", get_request_type_url(),
                   protobuf_mini_dumper_get_readable(*request_body_));
    }

    has_unpack_request_ = true;
    return true;
  }

  void pack_response() {
    has_pack_response_ = true;

    atframework::SSMsg &rsp = add_response_message();
    rsp.mutable_head()->set_error_code(get_response_code());
    init_msg(rsp, get_request_node_id(), get_request_node_name(), get_request());

    if (false == get_response_body().SerializeToString(rsp.mutable_body_bin())) {
      FCTXLOGERROR(get_shared_context(), "Try to serialize message {} failed, msg: {}", get_response_type_url(),
                   get_response_body().InitializationErrorString());
    } else {
      FCTXLOGDEBUG(get_shared_context(), "Serialize rpc response message {} success:\n{}", get_response_type_url(),
                   protobuf_mini_dumper_get_readable(get_response_body()));
    }
  }

 private:
  bool has_unpack_request_;
  bool has_pack_response_;
  rpc_request_type *request_body_;
  rpc_response_type *response_body_;
};
