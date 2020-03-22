//
// Created by owent on 2018/05/01.
//

#ifndef ROUTER_ACTION_TASK_ACTION_ROUTER_UPDATE_H
#define ROUTER_ACTION_TASK_ACTION_ROUTER_UPDATE_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_router_update : public task_action_ss_req_base {
public:
    typedef task_action_ss_req_base::msg_type msg_type;
    typedef task_action_ss_req_base::msg_ref_type msg_ref_type;
    typedef task_action_ss_req_base::msg_cref_type msg_cref_type;

    using task_action_ss_req_base::operator();

public:
    task_action_router_update(dispatcher_start_data_t COPP_MACRO_RV_REF param);
    ~task_action_router_update();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();
};


#endif //_ROUTER_ACTION_TASK_ACTION_ROUTER_UPDATE_H
