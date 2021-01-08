//
// Created by owt50 on 2016/11/14.
//

#include "actor_action_no_req_base.h"

actor_action_no_req_base::actor_action_no_req_base() {}

actor_action_no_req_base::~actor_action_no_req_base() {}

void actor_action_no_req_base::send_rsp_msg() {}

std::shared_ptr<dispatcher_implement> actor_action_no_req_base::get_dispatcher() const { return nullptr; }
