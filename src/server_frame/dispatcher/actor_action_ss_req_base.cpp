//
// Created by owt50 on 2016/11/14.
//

#include <log/log_wrapper.h>
#include <time/time_utility.h>


#include <dispatcher/ss_msg_dispatcher.h>

#include "actor_action_ss_req_base.h"

actor_action_ss_req_base::actor_action_ss_req_base(dispatcher_start_data_t COPP_MACRO_RV_REF start_param) {
    msg_type *ss_msg = ss_msg_dispatcher::me()->get_protobuf_msg<msg_type>(start_param.message);
    if (NULL != ss_msg) {
        get_request().Swap(ss_msg);

        set_player_id(get_request().head().player_user_id());
    }
}

actor_action_ss_req_base::~actor_action_ss_req_base() {}

uint64_t actor_action_ss_req_base::get_request_bus_id() const {
    msg_cref_type msg = get_request();
    return msg.head().bus_id();
}

hello::SSMsgBody &actor_action_ss_req_base::get_request_body() {
    hello::SSMsg& req_msg = get_request();
    if (!req_msg.body_bin().empty() && (!req_msg.has_body() || req_msg.body().body_oneof_case() == hello::SSMsgBody::BODY_ONEOF_NOT_SET)) {
        if(false == req_msg.mutable_body()->ParseFromString(req_msg.body_bin())) {
            WLOGERROR("task %s [%p] unpack message body failed, msg: %s", name(), this, req_msg.mutable_body()->InitializationErrorString().c_str());
        }
    }

    return *req_msg.mutable_body();
}

actor_action_ss_req_base::msg_ref_type actor_action_ss_req_base::add_rsp_msg(uint64_t dst_pd) {
    rsp_msgs_.push_back(msg_type());
    msg_ref_type msg = rsp_msgs_.back();

    msg.mutable_head()->set_error_code(get_rsp_code());
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

    return 0;
}

void actor_action_ss_req_base::send_rsp_msg() {
    if (rsp_msgs_.empty()) {
        return;
    }

    for (std::list<msg_type>::iterator iter = rsp_msgs_.begin(); iter != rsp_msgs_.end(); ++iter) {
        if (0 == (*iter).head().bus_id()) {
            WLOGERROR("actor %s [%p] send message to unknown server", name(), this);
            continue;
        }
        (*iter).mutable_head()->set_error_code(get_rsp_code());

        // send message using ss dispatcher
        int32_t res = ss_msg_dispatcher::me()->send_to_proc((*iter).head().bus_id(), *iter);
        if (res) {
            WLOGERROR("task %s [%p] send message to server 0x%llx failed, res: %d", name(), this, static_cast<unsigned long long>((*iter).head().bus_id()),
                      res);
        }
    }

    rsp_msgs_.clear();
}