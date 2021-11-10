//
// Created by owt50 on 2016/11/14.
//

#include <rpc/db/uuid.h>
#include <rpc/rpc_utils.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include "actor_action_no_req_base.h"

actor_action_no_req_base::ctor_param_t::ctor_param_t() : caller_context(nullptr) {}

actor_action_no_req_base::actor_action_no_req_base() {}

actor_action_no_req_base::actor_action_no_req_base(const ctor_param_t& param)
    : actor_action_base(param.caller_context) {}

actor_action_no_req_base::~actor_action_no_req_base() {}

void actor_action_no_req_base::send_response() {}

std::shared_ptr<dispatcher_implement> actor_action_no_req_base::get_dispatcher() const { return nullptr; }

const char* actor_action_no_req_base::get_type_name() const { return "background"; }
