// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <list>
#include <memory>
#include <utility>

#include "config/logic_config.h"
#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_action_base.h"

class session;
class player_cache;

class task_action_cs_req_base : public task_action_req_base<hello::CSMsg> {
 public:
  using base_type = task_action_req_base<hello::CSMsg>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = msg_type &;
  using msg_cref_type = const msg_type &;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_ret_code;
  using base_type::get_rsp_code;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_ret_code;
  using base_type::set_rsp_code;
  using base_type::operator();

 public:
  explicit task_action_cs_req_base(dispatcher_start_data_t &&start_param);
  virtual ~task_action_cs_req_base();

  int hook_run() override;

  std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  const char *get_type_name() const override;
  const atframework::RpcTraceSpan *get_parent_trace_span() const override;

  std::pair<uint64_t, uint64_t> get_gateway_info() const;

  std::shared_ptr<session> get_session() const;

  msg_ref_type add_rsp_msg();

  std::list<msg_type *> &get_rsp_list();
  const std::list<msg_type *> &get_rsp_list() const;

  std::shared_ptr<player_cache> get_player_cache() const;

  template <typename TPLAYER>
  std::shared_ptr<TPLAYER> get_player() const {
    return std::static_pointer_cast<TPLAYER>(get_player_cache());
  }

 protected:
  void send_rsp_msg() override;
  void send_rsp_msg(bool sync_dirty);

 private:
  mutable std::shared_ptr<session> session_inst_;
  std::list<msg_type *> rsp_msgs_;
  bool has_sync_dirty_;
};

template <class TReqType, class TRspType>
class task_action_cs_rpc_base : public task_action_cs_req_base {
 public:
  using base_type = task_action_cs_req_base;
  using msg_type = base_type::msg_type;
  using msg_ref_type = msg_type &;
  using msg_cref_type = const msg_type &;
  using rpc_request_type = TReqType;
  using rpc_response_type = TRspType;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_ret_code;
  using base_type::get_rsp_code;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_ret_code;
  using base_type::set_rsp_code;
  using base_type::operator();

 public:
  explicit task_action_cs_rpc_base(dispatcher_start_data_t &&start_param)
      : base_type(COPP_MACRO_STD_MOVE(start_param)),
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

  static const std::string &get_request_type_url() { return rpc_request_type::descriptor()->full_name(); }

  static const std::string &get_response_type_url() { return rpc_response_type::descriptor()->full_name(); }

  virtual bool is_stream_rpc() const { return get_request().head().has_rpc_stream(); }

 protected:
  void send_rsp_msg() override {
    if (!has_pack_response_ && !is_stream_rpc()) {
      pack_response();
    }
    base_type::send_rsp_msg();
  }

 private:
  void unpack_request() {
    has_unpack_request_ = true;

    request_body_ = get_shared_context().template create<rpc_request_type>();
    if (nullptr == request_body_) {
      return;
    }

    // Check message type
    if (get_request().head().has_rpc_request()) {
      if (get_request_type_url() != get_request().head().rpc_request().type_url()) {
        FWLOGERROR("task {} [{}] except message {}, real got {}", name(), get_task_id(), get_request_type_url(),
                   get_request().head().rpc_request().type_url());
      }
    } else if (get_request().head().has_rpc_stream()) {
      if (get_request_type_url() != get_request().head().rpc_stream().type_url()) {
        FWLOGERROR("task {} [{}] except message {}, real got {}", name(), get_task_id(), get_request_type_url(),
                   get_request().head().rpc_stream().type_url());
      }
    }

    if (false == request_body_->ParseFromString(get_request().body_bin())) {
      FWLOGERROR("task {} [{}] try to parse message {} failed, msg: {}", name(), get_task_id(), get_request_type_url(),
                 request_body_->InitializationErrorString());
    } else {
      FWLOGDEBUG("task {} [{}] parse rpc request message {} success:\n{}", name(), get_task_id(),
                 get_request_type_url(), protobuf_mini_dumper_get_readable(*request_body_));
    }
  }

  void pack_response() {
    has_pack_response_ = true;

    hello::CSMsg &rsp = add_rsp_msg();
    hello::CSMsgHead *head = rsp.mutable_head();
    if (nullptr == head) {
      FWLOGERROR("task {} [{}] pack response but malloc header failed", name(), get_task_id());
      return;
    }

    if (get_request().head().has_rpc_request()) {
      head->clear_rpc_request();
      head->mutable_rpc_response()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
      head->mutable_rpc_response()->set_rpc_name(get_request().head().rpc_request().rpc_name());
      head->mutable_rpc_response()->set_type_url(get_response_type_url());
    } else {
      head->clear_rpc_stream();
      head->mutable_rpc_stream()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
      head->mutable_rpc_stream()->set_rpc_name(get_request().head().rpc_stream().rpc_name());
      head->mutable_rpc_stream()->set_type_url(get_response_type_url());

      head->mutable_rpc_stream()->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
      head->mutable_rpc_stream()->set_callee(get_request().head().rpc_stream().caller());
    }

    if (nullptr != response_body_) {
      if (false == response_body_->SerializeToString(rsp.mutable_body_bin())) {
        FWLOGERROR("task {} [{}] try to serialize message {} failed, msg: {}", name(), get_task_id(),
                   get_response_type_url(), response_body_->InitializationErrorString());
      } else {
        FWLOGDEBUG("task {} [{}] serialize rpc response message {} success:\n{}", name(), get_task_id(),
                   get_response_type_url(), protobuf_mini_dumper_get_readable(*response_body_));
      }
    }
  }

 private:
  bool has_unpack_request_;
  bool has_pack_response_;
  rpc_request_type *request_body_;
  rpc_response_type *response_body_;
};
