//
// Created by owt50 on 2016/9/26.
//

#include <rpc/db/uuid.h>
#include <rpc/rpc_utils.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include "task_action_no_req_base.h"

task_action_no_req_base::ctor_param_t::ctor_param_t() : caller_context(nullptr) {}

task_action_no_req_base::task_action_no_req_base() { get_shared_context().set_trace_id(rpc::db::uuid::generate_standard_uuid(true)); }

task_action_no_req_base::task_action_no_req_base(const ctor_param_t &param) : task_action_base(param.caller_context) {
    if (nullptr == get_shared_context().get_trace_span() || get_shared_context().get_trace_span()->trace_id().empty()) {
        get_shared_context().set_trace_id(rpc::db::uuid::generate_standard_uuid(true));
    }
}

task_action_no_req_base::~task_action_no_req_base() {}

void task_action_no_req_base::send_rsp_msg() {}

std::shared_ptr<dispatcher_implement> task_action_no_req_base::get_dispatcher() const { return nullptr; }
