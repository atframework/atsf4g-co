//
// Created by owt50 on 2016/11/14.
//

#ifndef DISPATCHER_ACTOR_ACTION_NO_REQ_BASE_H
#define DISPATCHER_ACTOR_ACTION_NO_REQ_BASE_H

#pragma once

#include "actor_action_base.h"

class actor_action_no_req_base : public actor_action_base {
#if defined(UTIL_CONFIG_COMPILER_CXX_ALIAS_TEMPLATES) && UTIL_CONFIG_COMPILER_CXX_ALIAS_TEMPLATES
    using base_type = actor_action_base;
#else
    typedef actor_action_base base_type;
#endif

    struct ctor_param_t {
        rpc::context *caller_context;

        ctor_param_t();
    };

public:
    using base_type::get_ret_code;
    using base_type::get_rsp_code;
    using base_type::name;
    using base_type::set_ret_code;
    using base_type::set_rsp_code;
    using base_type::operator();

public:
    actor_action_no_req_base();
    actor_action_no_req_base(const ctor_param_t &param);
    ~actor_action_no_req_base();

    std::shared_ptr<dispatcher_implement> get_dispatcher() const UTIL_CONFIG_OVERRIDE;

protected:
    virtual void send_rsp_msg() UTIL_CONFIG_OVERRIDE;
};


#endif //_DISPATCHER_ACTOR_ACTION_NO_REQ_BASE_H
