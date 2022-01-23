// Copyright 2021 atframework
// Created by owent on 2016/11/14.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <list>
#include <memory>

#include "dispatcher/dispatcher_type_defines.h"

#include "dispatcher/actor_action_base.h"

class actor_action_ss_req_base : public actor_action_req_base<atframework::SSMsg> {
 public:
  using base_type = actor_action_req_base<atframework::SSMsg>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = msg_type&;
  using msg_cref_type = const msg_type;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  explicit actor_action_ss_req_base(dispatcher_start_data_t&& start_param);
  virtual ~actor_action_ss_req_base();

  uint64_t get_request_bus_id() const;

  msg_ref_type add_rsp_msg(uint64_t dst_pd = 0);

  static int32_t init_msg(msg_ref_type msg, uint64_t dst_pd);
  static int32_t init_msg(msg_ref_type msg, uint64_t dst_pd, msg_cref_type req_msg);

  std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  const char* get_type_name() const override;
  const atframework::RpcTraceSpan* get_parent_trace_span() const override;

 protected:
  void send_response() override;

 private:
  std::list<msg_type> response_messages_;
};
