// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_no_req_base.h"

#include <rpc/db/uuid.h>
#include <rpc/rpc_utils.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

task_action_no_req_base::ctor_param_t::ctor_param_t() : caller_context(nullptr) {}

task_action_no_req_base::task_action_no_req_base() {}

task_action_no_req_base::task_action_no_req_base(const ctor_param_t& param) : task_action_base(param.caller_context) {}

task_action_no_req_base::~task_action_no_req_base() {}

void task_action_no_req_base::send_response() {}

std::shared_ptr<dispatcher_implement> task_action_no_req_base::get_dispatcher() const { return nullptr; }

const char* task_action_no_req_base::get_type_name() const { return "background"; }
