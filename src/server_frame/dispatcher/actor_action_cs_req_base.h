//
// Created by owt50 on 2016/11/14.
//

#ifndef DISPATCHER_ACTOR_ACRION_CS_REQ_BASE_H
#define DISPATCHER_ACTOR_ACRION_CS_REQ_BASE_H

#pragma once

#include "dispatcher_type_defines.h"

#include "actor_action_base.h"

#include <protocol/pbdesc/com.protocol.pb.h>

class session;

class actor_action_cs_req_base : public actor_action_req_base<hello::CSMsg> {
public:
    typedef actor_action_req_base<hello::CSMsg> base_type;
    typedef base_type::msg_type                 msg_type;
    typedef msg_type &                          msg_ref_type;
    typedef const msg_type &                    msg_cref_type;

protected:
    using base_type::get_request;

public:
    using base_type::get_ret_code;
    using base_type::get_rsp_code;
    using base_type::name;
    using base_type::set_ret_code;
    using base_type::set_rsp_code;
    using base_type::operator();

public:
    actor_action_cs_req_base(dispatcher_start_data_t COPP_MACRO_RV_REF start_param);
    virtual ~actor_action_cs_req_base();

    std::pair<uint64_t, uint64_t> get_gateway_info() const;

    std::shared_ptr<session> get_session() const;

    std::shared_ptr<player_cache> get_player_cache() const;

    template <typename TPLAYER>
    std::shared_ptr<TPLAYER> get_player() const {
        return std::static_pointer_cast<TPLAYER>(get_player_cache());
    }

    msg_ref_type add_rsp_msg();

    std::list<msg_type> &      get_rsp_list();
    const std::list<msg_type> &get_rsp_list() const;

protected:
    virtual void send_rsp_msg() UTIL_CONFIG_OVERRIDE;

private:
    mutable std::shared_ptr<session> session_inst_;
    std::list<msg_type>              rsp_msgs_;
};


#endif //_DISPATCHER_ACTOR_ACRION_CS_REQ_BASE_H
