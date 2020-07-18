//
// Created by owent on 2016/10/6.
//

#ifndef LOGIC_ACTION_TASK_ACTION_PING_H
#define LOGIC_ACTION_TASK_ACTION_PING_H

#pragma once

#include <dispatcher/task_action_cs_req_base.h>

class task_action_ping : public task_action_cs_req_base {
public:
    using task_action_cs_req_base::operator();

public:
    task_action_ping(dispatcher_start_data_t COPP_MACRO_RV_REF param);
    ~task_action_ping();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();
};


#endif //_LOGIC_ACTION_TASK_ACTION_PING_H
