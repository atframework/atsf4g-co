// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <memory/rc_ptr.h>
#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

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

class ATFW_UTIL_SYMBOL_VISIBLE task_action_cs_req_base : public task_action_req_base<atframework::CSMsg> {
 public:
  using base_type = task_action_req_base<atframework::CSMsg>;
  using message_type = base_type::message_type;
  using result_type = base_type::result_type;
  using msg_ref_type = message_type &;
  using msg_cref_type = const message_type &;

 protected:
  using base_type::get_request;

 private:
  struct gateway_info_t;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  SERVER_FRAME_API explicit task_action_cs_req_base(dispatcher_start_data_type &&start_param);
  SERVER_FRAME_API virtual ~task_action_cs_req_base();

  SERVER_FRAME_API result_type hook_run() override;

  SERVER_FRAME_API std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  SERVER_FRAME_API const char *get_type_name() const override;

  SERVER_FRAME_API static void add_prepare_handle(rpc::result_code_type (*fn)(rpc::context &,
                                                                              task_action_cs_req_base &));

  SERVER_FRAME_API rpc::telemetry::trace_start_option get_trace_option() const noexcept override;

  SERVER_FRAME_API virtual bool is_stream_rpc() const noexcept;

  virtual bool unpack_request() noexcept = 0;

  virtual atfw::util::nostd::string_view get_request_type_url() const noexcept = 0;

  virtual atfw::util::nostd::string_view get_response_type_url() const noexcept = 0;

  SERVER_FRAME_API uint64_t get_gateway_node_id() const noexcept;

  SERVER_FRAME_API const std::string &get_gateway_node_name() const noexcept;

  SERVER_FRAME_API uint64_t get_gateway_session_id() const noexcept;

  SERVER_FRAME_API std::shared_ptr<session> get_session() const;

  SERVER_FRAME_API msg_ref_type add_response_message();

  SERVER_FRAME_API std::list<message_type *> &get_rsp_list();
  SERVER_FRAME_API const std::list<message_type *> &get_rsp_list() const;

  SERVER_FRAME_API std::shared_ptr<player_cache> get_player_cache() const;

  template <typename TPLAYER>
  ATFW_UTIL_FORCEINLINE std::shared_ptr<TPLAYER> get_player() const {
    return std::static_pointer_cast<TPLAYER>(get_player_cache());
  }

 private:
  const gateway_info_t &get_gateway_info() const noexcept;

 protected:
  SERVER_FRAME_API void write_actor_log_head();
  SERVER_FRAME_API void send_response() override;
  SERVER_FRAME_API void send_response(bool sync_dirty);
  SERVER_FRAME_API void write_actor_log_body(const google::protobuf::Message &msg, const atframework::CSMsgHead &head,
                                             bool is_input);

  ATFW_UTIL_FORCEINLINE bool has_response_message() const noexcept { return !response_messages_.empty(); }

 private:
  mutable std::shared_ptr<session> session_inst_;
  mutable atfw::util::memory::strong_rc_ptr<gateway_info_t> gateway_info_t_;
  std::list<message_type *> response_messages_;
  bool has_sync_dirty_;
  bool recursive_sync_dirty_;

  static std::list<rpc::result_code_type (*)(rpc::context &, task_action_cs_req_base &)> prepare_handles_;
};

template <class TReqType, class TRspType>
class ATFW_UTIL_SYMBOL_VISIBLE task_action_cs_rpc_base : public task_action_cs_req_base {
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
      response_body_ = create_message_at_task_arena<rpc_response_type>();
    }

    if (nullptr == response_body_) {
      static rpc_response_type empty_msg;
      empty_msg.Clear();
      return empty_msg;
    }

    return *response_body_;
  }

  atfw::util::nostd::string_view get_request_type_url() const noexcept override {
    decltype(auto) full_name = rpc_request_type::descriptor()->full_name();
    return {atfw::util::nostd::data(full_name), atfw::util::nostd::size(full_name)};
  }

  atfw::util::nostd::string_view get_response_type_url() const noexcept override {
    decltype(auto) full_name = rpc_response_type::descriptor()->full_name();
    return {atfw::util::nostd::data(full_name), atfw::util::nostd::size(full_name)};
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
      request_body_ = create_message_at_task_arena<rpc_request_type>();
    }
    if (nullptr == request_body_) {
      return false;
    }

    // Check message type
    if (get_request().head().has_rpc_request()) {
      decltype(auto) type_url = get_request().head().rpc_request().type_url();
      atfw::util::nostd::string_view type_url_view{atfw::util::nostd::data(type_url),
                                                   atfw::util::nostd::size(type_url)};
      if (get_request_type_url() != type_url_view) {
        FWLOGERROR("{}Except message {}, real got {}", get_shared_context_log_prefix(), get_request_type_url(),
                   type_url_view);
        return false;
      }
    } else if (get_request().head().has_rpc_stream()) {
      decltype(auto) type_url = get_request().head().rpc_stream().type_url();
      atfw::util::nostd::string_view type_url_view{atfw::util::nostd::data(type_url),
                                                   atfw::util::nostd::size(type_url)};
      if (get_request_type_url() != type_url_view) {
        FWLOGERROR("{}Except message {}, real got {}", get_shared_context_log_prefix(), get_request_type_url(),
                   type_url_view);
        return false;
      }
    }

    if (false == request_body_->ParseFromString(get_request().body_bin())) {
      FWLOGERROR("{}Try to parse message {} failed, message: {}", get_shared_context_log_prefix(), get_request_type_url(),
                request_body_->InitializationErrorString());
      return false;
    } else {
      FWLOGDEBUG("{}Parse rpc request message {} success:\n{}", get_shared_context_log_prefix(), get_request_type_url(),
                protobuf_mini_dumper_get_readable(*request_body_));
    }

    has_unpack_request_ = true;
    return true;
  }

  void pack_response() {
    has_pack_response_ = true;

    atframework::CSMsg &rsp = add_response_message();

    if (false == get_response_body().SerializeToString(rsp.mutable_body_bin())) {
      FWLOGERROR("{}Try to serialize message {} failed, success: {}", get_shared_context_log_prefix(),
                get_response_type_url(), get_response_body().InitializationErrorString());
    } else {
      FWLOGDEBUG("{}Serialize rpc response message {} success:\n{}", get_shared_context_log_prefix(),
                get_response_type_url(), protobuf_mini_dumper_get_readable(get_response_body()));
    }
  }

 private:
  bool has_unpack_request_;
  bool has_pack_response_;
  rpc_request_type *request_body_;
  rpc_response_type *response_body_;
};
