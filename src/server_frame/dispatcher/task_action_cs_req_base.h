// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "config/logic_config.h"
#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_action_base.h"
#include "utility/protobuf_mini_dumper.h"

class session;
class player_cache;

class task_action_cs_req_base : public task_action_req_base<atframework::CSMsg> {
 public:
  using base_type = task_action_req_base<atframework::CSMsg>;
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
  explicit task_action_cs_req_base(dispatcher_start_data_type &&start_param);
  virtual ~task_action_cs_req_base();

  result_type hook_run() override;

  std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  const char *get_type_name() const override;

  rpc::context::trace_start_option get_trace_option() const noexcept override;

  virtual bool is_stream_rpc() const noexcept;

  virtual const std::string &get_request_type_url() const noexcept = 0;

  virtual const std::string &get_response_type_url() const noexcept = 0;

  std::pair<uint64_t, uint64_t> get_gateway_info() const;

  std::shared_ptr<session> get_session() const;

  msg_ref_type add_response_message();

  std::list<message_type *> &get_rsp_list();
  const std::list<message_type *> &get_rsp_list() const;

  std::shared_ptr<player_cache> get_player_cache() const;

  template <typename TPLAYER>
  std::shared_ptr<TPLAYER> get_player() const {
    return std::static_pointer_cast<TPLAYER>(get_player_cache());
  }

 protected:
  void send_response() override;
  void send_response(bool sync_dirty);
  void write_actor_log_body(const google::protobuf::Message &msg, const atframework::CSMsgHead &head);

  inline bool has_response_message() const noexcept { return !response_messages_.empty(); }

 private:
  mutable std::shared_ptr<session> session_inst_;
  std::list<message_type *> response_messages_;
  bool has_sync_dirty_;
  bool recursive_sync_dirty_;
};

template <class TReqType, class TRspType>
class UTIL_SYMBOL_VISIBLE task_action_cs_rpc_base : public task_action_cs_req_base {
 public:
  using base_type = task_action_cs_req_base;
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
  explicit task_action_cs_rpc_base(dispatcher_start_data_type &&start_param)
      : base_type(std::move(start_param)),
        has_unpack_request_(false),
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
    if (!is_stream_rpc() && !has_response_message() && is_response_message_enabled()) {
      pack_response();
    }
    base_type::send_response();
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
      write_actor_log_body(*request_body_, get_request().head());
    }
  }

  void pack_response() {
    atframework::CSMsg &rsp = add_response_message();

    if (false == get_response_body().SerializeToString(rsp.mutable_body_bin())) {
      FWLOGERROR("task {} [{}] try to serialize message {} failed, msg: {}", name(), get_task_id(),
                 get_response_type_url(), get_response_body().InitializationErrorString());
    } else {
      FWLOGDEBUG("task {} [{}] serialize rpc response message {} success:\n{}", name(), get_task_id(),
                 get_response_type_url(), protobuf_mini_dumper_get_readable(get_response_body()));
      write_actor_log_body(get_response_body(), rsp.head());
    }
  }

 private:
  bool has_unpack_request_;
  rpc_request_type *request_body_;
  rpc_response_type *response_body_;
};
