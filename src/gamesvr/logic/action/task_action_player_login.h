//
// Created by owent on 2016/10/6.
//

#ifndef LOGIC_ACTION_TASK_ACTION_PLAYER_LOGIN_H
#define LOGIC_ACTION_TASK_ACTION_PLAYER_LOGIN_H

#pragma once

#include <dispatcher/task_action_cs_req_base.h>

class task_action_player_login : public task_action_cs_req_base {
public:
    typedef task_action_cs_req_base::msg_type msg_type;
    typedef task_action_cs_req_base::msg_ref_type msg_ref_type;
    typedef task_action_cs_req_base::msg_cref_type msg_cref_type;

    using task_action_cs_req_base::operator();

public:
    task_action_player_login(dispatcher_start_data_t COPP_MACRO_RV_REF param);
    ~task_action_player_login();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();

private:
    bool is_new_player_;
    uint32_t zone_id_;
};


#endif //_LOGIC_ACTION_TASK_ACTION_PLAYER_LOGIN_H
