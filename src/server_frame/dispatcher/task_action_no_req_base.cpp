//
// Created by owt50 on 2016/9/26.
//

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <rpc/db/uuid.h>

#include "task_action_no_req_base.h"


task_action_no_req_base::task_action_no_req_base() { get_shared_context().set_trace_id(rpc::db::uuid::generate_short_uuid()); }

task_action_no_req_base::~task_action_no_req_base() {}

void task_action_no_req_base::send_rsp_msg() {}
