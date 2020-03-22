//
// Created by owt50 on 2016/11/14.
//

#include <data/player_cache.h>
#include <log/log_wrapper.h>
#include <logic/session_manager.h>
#include <time/time_utility.h>

// #include <router/router_player_cache.h>
#include <router/router_player_manager.h>

#include <dispatcher/cs_msg_dispatcher.h>

#include "actor_action_cs_req_base.h"

actor_action_cs_req_base::actor_action_cs_req_base(dispatcher_start_data_t COPP_MACRO_RV_REF start_param) {
    msg_type *cs_msg = cs_msg_dispatcher::me()->get_protobuf_msg<msg_type>(start_param.message);
    if (NULL != cs_msg) {
        get_request().Swap(cs_msg);

        session::ptr_t sess = get_session();
        if (sess) {
            player_cache::ptr_t player_cache = sess->get_player();
            if (player_cache) {
                set_player_id(player_cache->get_user_id());
            }
        }
    }
}

actor_action_cs_req_base::~actor_action_cs_req_base() {}

std::pair<uint64_t, uint64_t> actor_action_cs_req_base::get_gateway_info() const {
    const msg_type &cs_msg = get_request();
    return std::pair<uint64_t, uint64_t>(cs_msg.head().session_bus_id(), cs_msg.head().session_id());
};

std::shared_ptr<session> actor_action_cs_req_base::get_session() const {
    if (session_inst_) {
        return session_inst_;
    }

    session::key_t key(get_gateway_info());
    session_inst_ = session_manager::me()->find(key);
    return session_inst_;
}

std::shared_ptr<player_cache> actor_action_cs_req_base::get_player_cache() const {
    std::shared_ptr<session> sess = get_session();
    if (!sess) {
        return NULL;
    }

    return sess->get_player();
}

actor_action_cs_req_base::msg_ref_type actor_action_cs_req_base::add_rsp_msg() {
    rsp_msgs_.push_back(msg_type());
    return rsp_msgs_.back();
}

std::list<actor_action_cs_req_base::msg_type> &actor_action_cs_req_base::get_rsp_list() { return rsp_msgs_; }

const std::list<actor_action_cs_req_base::msg_type> &actor_action_cs_req_base::get_rsp_list() const { return rsp_msgs_; }

void actor_action_cs_req_base::send_rsp_msg() {
    if (rsp_msgs_.empty()) {
        return;
    }

    session::ptr_t sess = get_session();
    if (!sess) {
        std::pair<uint64_t, uint64_t> sess_id = get_gateway_info();
        WLOGERROR("try to send response message, but session (0x%llx, 0x%llx) not found", static_cast<unsigned long long>(sess_id.first),
                  static_cast<unsigned long long>(sess_id.second));
        return;
    }

    player_cache::ptr_t owner_player = sess->get_player();

    uint32_t seq = 0;
    {
        msg_ref_type req_msg = get_request();
        if (req_msg.has_head()) {
            seq = req_msg.head().sequence();
        }
    }

    for (std::list<msg_type>::iterator iter = rsp_msgs_.begin(); iter != rsp_msgs_.end(); ++iter) {
        (*iter).mutable_head()->set_error_code(get_rsp_code());
        (*iter).mutable_head()->set_timestamp(util::time::time_utility::get_now());
        (*iter).mutable_head()->set_sequence(seq);

        // send message using session
        int32_t res = sess->send_msg_to_client(*iter);
        if (res) {
            if (owner_player) {
                WLOGERROR("task %s [%p] send message to player_cache %s failed, res: %d", name(), this, owner_player->get_open_id().c_str(), res);
            } else {
                WLOGERROR("task %s [%p] send message to session [0x%llx,0x%llx] failed, res: %d", name(), this,
                          static_cast<unsigned long long>(sess->get_key().bus_id), static_cast<unsigned long long>(sess->get_key().session_id), res);
            }
        }
    }

    rsp_msgs_.clear();

    // sync messages
    if (owner_player) {
        owner_player->send_all_syn_msg();

        // refresh visit time if success
        if (0 == get_rsp_code()) {
            router_player_manager::ptr_t router_cache =
                router_player_manager::me()->get_cache(router_player_manager::key_t(router_player_manager::me()->get_type_id(), owner_player->get_zone_id(), owner_player->get_user_id()));
            if (router_cache && router_cache->is_object_equal(owner_player)) {
                router_cache->refresh_visit_time();
            }
        }
    }
}