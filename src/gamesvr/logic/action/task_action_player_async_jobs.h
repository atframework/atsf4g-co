//
// Created by owent on 2018/05/09.
//

#ifndef ROUTER_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_H
#define ROUTER_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_H

#pragma once

#include <std/smart_ptr.h>

#include <dispatcher/task_action_no_req_base.h>

class player;

class task_action_player_async_jobs : public task_action_no_req_base {
public:
    struct ctor_param_t {
        std::shared_ptr<player> user;
    };

public:
    using task_action_no_req_base::operator();

public:
    task_action_player_async_jobs(ctor_param_t COPP_MACRO_RV_REF param);
    ~task_action_player_async_jobs();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();

private:
    ctor_param_t param_;
};


#endif // ROUTER_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_H
