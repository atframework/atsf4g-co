//
// Created by owt50 on 2016/9/26.
//

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <rpc/db/uuid.h>

#include "task_action_no_req_base.h"


task_action_no_req_base::task_action_no_req_base() { get_shared_context().set_trace_id(rpc::db::uuid::generate_standard_uuid(true)); }

task_action_no_req_base::~task_action_no_req_base() {}

void task_action_no_req_base::send_rsp_msg() {}

std::shared_ptr<dispatcher_implement> task_action_no_req_base::get_dispatcher() const { return nullptr; }
