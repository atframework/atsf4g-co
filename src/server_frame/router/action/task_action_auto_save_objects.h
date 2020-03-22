//
// Created by owent on 2018/05/01.
//

#ifndef ROUTER_ACTION_TASK_ACTION_AUTO_SAVE_OBJECTS_H
#define ROUTER_ACTION_TASK_ACTION_AUTO_SAVE_OBJECTS_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

class task_action_auto_save_objects : public task_action_no_req_base {
public:
    struct ctor_param_t {};

public:
    using task_action_no_req_base::operator();

public:
    task_action_auto_save_objects(ctor_param_t COPP_MACRO_RV_REF param);
    ~task_action_auto_save_objects();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();
    virtual int on_timeout();

private:
    const char *get_action_name(uint32_t) const;

private:
    int success_count_;
    int failed_count_;
};


#endif //_ROUTER_ACTION_TASK_ACTION_AUTO_SAVE_OBJECTS_H
