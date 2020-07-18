//
// Created by owent on 2019/06/20.
//

#ifndef ROUTER_ACTION_TASK_ACTION_ROUTER_CLOSE_MANAGER_SET_H
#define ROUTER_ACTION_TASK_ACTION_ROUTER_CLOSE_MANAGER_SET_H

#pragma once

#include <std/smart_ptr.h>
#include <vector>

#include <dispatcher/task_action_no_req_base.h>

class router_object_base;

class task_action_router_close_manager_set : public task_action_no_req_base {
public:
    typedef std::shared_ptr<router_object_base> router_object_ptr_t;
    typedef std::vector<router_object_ptr_t>    pending_list_t;
    typedef std::shared_ptr<pending_list_t>     pending_list_ptr_t;

    struct ctor_param_t {
        pending_list_ptr_t pending_list;
    };

public:
    using task_action_no_req_base::operator();

public:
    task_action_router_close_manager_set(ctor_param_t COPP_MACRO_RV_REF param);
    ~task_action_router_close_manager_set();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();
    virtual int on_timeout();

private:
    void save_fallback();

private:
    ctor_param_t param_;
    int          success_count_;
    int          failed_count_;
    size_t       current_idx_;
};


#endif // ROUTER_ACTION_TASK_ACTION_ROUTER_CLOSE_MANAGER_SET_H
