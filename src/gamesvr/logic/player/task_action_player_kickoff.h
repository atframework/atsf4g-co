/**
 * @brief Created by owent with generate-for-pb.py at 2020-07-10 22:02:19
 */

#ifndef GENERATED_ACTION_TASK_ACTION_PLAYER_KICKOFF_H
#define GENERATED_ACTION_TASK_ACTION_PLAYER_KICKOFF_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_player_kickoff : public task_action_ss_rpc_base<hello::SSPlayerKickOffReq, hello::SSPlayerKickOffRsp> {
public:
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    using base_type = task_action_ss_rpc_base<hello::SSPlayerKickOffReq, hello::SSPlayerKickOffRsp>;
    using msg_type = base_type::msg_type;
    using msg_ref_type = base_type::msg_ref_type;
    using msg_cref_type = base_type::msg_cref_type;
    using rpc_request_type  = base_type::rpc_request_type;
    using rpc_response_type = base_type::rpc_response_type;
#else
    typedef task_action_ss_rpc_base<hello::SSPlayerKickOffReq, hello::SSPlayerKickOffRsp> base_type;
    typedef base_type::msg_type msg_type;
    typedef base_type::msg_ref_type msg_ref_type;
    typedef base_type::msg_cref_type msg_cref_type;
    typedef base_type::rpc_request_type rpc_request_type;
    typedef base_type::rpc_response_type rpc_response_type;
#endif

    using task_action_ss_req_base::operator();

public:
    task_action_player_kickoff(dispatcher_start_data_t COPP_MACRO_RV_REF param);
    ~task_action_player_kickoff();

    virtual bool is_stream_rpc() const UTIL_CONFIG_OVERRIDE;
    
    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();
};

#endif // GENERATED_TASK_ACTION_PLAYER_KICKOFF_H

