// Copyright 2021 atframework
// Created by owent on 2016/11/14.
//

#include "dispatcher/actor_action_ss_req_base.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <rpc/db/uuid.h>

actor_action_ss_req_base::actor_action_ss_req_base(dispatcher_start_data_t &&start_param) {
  // 必须先设置共享的arena
  if (nullptr != start_param.context) {
    get_shared_context().set_parent_context(*start_param.context);
  }

  msg_type *ss_msg = ss_msg_dispatcher::me()->get_protobuf_msg<msg_type>(start_param.message);
  if (nullptr != ss_msg) {
    get_request().Swap(ss_msg);

    set_user_key(get_request().head().player_user_id(), get_request().head().player_zone_id());
  }
}

actor_action_ss_req_base::~actor_action_ss_req_base() {}

uint64_t actor_action_ss_req_base::get_request_bus_id() const {
  msg_cref_type msg = get_request();
  return msg.head().bus_id();
}

actor_action_ss_req_base::msg_ref_type actor_action_ss_req_base::add_rsp_msg(uint64_t dst_pd) {
  response_messages_.push_back(msg_type());
  msg_ref_type msg = response_messages_.back();

  msg.mutable_head()->set_error_code(get_response_code());
  dst_pd = 0 == dst_pd ? get_request_bus_id() : dst_pd;

  init_msg(msg, dst_pd, get_request());

  return msg;
}

int32_t actor_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd) {
  msg.mutable_head()->set_bus_id(dst_pd);
  msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());

  return 0;
}

int32_t actor_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd, msg_cref_type req_msg) {
  protobuf_copy_message(*msg.mutable_head(), req_msg.head());
  init_msg(msg, dst_pd);

  // set task information
  if (0 != req_msg.head().src_task_id()) {
    msg.mutable_head()->set_dst_task_id(req_msg.head().src_task_id());
  } else {
    msg.mutable_head()->set_dst_task_id(0);
  }

  if (0 != req_msg.head().dst_task_id()) {
    msg.mutable_head()->set_src_task_id(req_msg.head().dst_task_id());
  } else {
    msg.mutable_head()->set_src_task_id(0);
  }

  msg.mutable_head()->set_sequence(req_msg.head().sequence());
  if (PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM == req_msg.head().op_type()) {
    msg.mutable_head()->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM);
  } else {
    msg.mutable_head()->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  }

  return 0;
}

void actor_action_ss_req_base::send_response() {
  if (response_messages_.empty()) {
    return;
  }

  for (std::list<msg_type>::iterator iter = response_messages_.begin(); iter != response_messages_.end(); ++iter) {
    if (0 == (*iter).head().bus_id()) {
      FWLOGERROR("actor {} [{}] send message to unknown server", name(), (const void *)this);
      continue;
    }
    (*iter).mutable_head()->set_error_code(get_response_code());

    // send message using ss dispatcher
    int32_t res = ss_msg_dispatcher::me()->send_to_proc((*iter).head().bus_id(), *iter);
    if (res) {
      FWLOGERROR("task {} [{}] send message to server 0x{:x} failed, res: {}({})", name(), (const void *)this,
                 (*iter).head().bus_id(), res, protobuf_mini_dumper_get_error_msg(res));
    }
  }

  response_messages_.clear();
}

std::shared_ptr<dispatcher_implement> actor_action_ss_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
}

const char *actor_action_ss_req_base::get_type_name() const { return "inserver"; }
