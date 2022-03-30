// Copyright 2021 atframework
// Created by owent on 2016-11-14.
//

#include "dispatcher/actor_action_cs_req_base.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <data/player_cache.h>
#include <logic/session_manager.h>

// #include <router/router_player_cache.h>
#include <router/router_player_manager.h>

#include <dispatcher/cs_msg_dispatcher.h>

actor_action_cs_req_base::actor_action_cs_req_base(dispatcher_start_data_t &&start_param) : has_sync_dirty_(false) {
  // 必须先设置共享的arena
  if (nullptr != start_param.context) {
    get_shared_context().try_reuse_protobuf_arena(start_param.context->mutable_protobuf_arena());
  }

  msg_type *cs_msg = cs_msg_dispatcher::me()->get_protobuf_msg<msg_type>(start_param.message);
  if (nullptr != cs_msg) {
    get_request().Swap(cs_msg);

    session::ptr_t sess = get_session();
    if (sess) {
      player_cache::ptr_t player_cache = sess->get_player();
      if (player_cache) {
        set_user_key(player_cache->get_user_id(), player_cache->get_zone_id());
      }
    }
  }

  // 最后设置 caller
  if (nullptr != start_param.context) {
    set_caller_context(*start_param.context);
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
    return nullptr;
  }

  return sess->get_player();
}

actor_action_cs_req_base::msg_ref_type actor_action_cs_req_base::add_rsp_msg() {
  response_messages_.push_back(msg_type());
  return response_messages_.back();
}

std::list<actor_action_cs_req_base::msg_type> &actor_action_cs_req_base::get_rsp_list() { return response_messages_; }

const std::list<actor_action_cs_req_base::msg_type> &actor_action_cs_req_base::get_rsp_list() const {
  return response_messages_;
}

void actor_action_cs_req_base::send_response() { send_response(!has_sync_dirty_); }

void actor_action_cs_req_base::send_response(bool sync_dirty) {
  if (response_messages_.empty() && !sync_dirty) {
    return;
  }

  session::ptr_t sess = get_session();
  if (!sess) {
    std::pair<uint64_t, uint64_t> sess_id = get_gateway_info();
    FWLOGERROR("try to send response message, but session [{:#x}, {}] not found", sess_id.first, sess_id.second);
    return;
  }

  player_cache::ptr_t owner_player = sess->get_player();
  // sync messages
  if (owner_player) {
    owner_player->send_all_syn_msg(get_shared_context());
    has_sync_dirty_ = true;

    // refresh visit time if success
    if (0 == get_response_code()) {
      router_player_manager::ptr_t router_cache = router_player_manager::me()->get_cache(router_player_manager::key_t(
          router_player_manager::me()->get_type_id(), owner_player->get_zone_id(), owner_player->get_user_id()));
      if (router_cache && router_cache->is_object_equal(owner_player)) {
        router_cache->refresh_visit_time();
        router_cache->trace_router(get_shared_context());
      }
    }
  }

  if (response_messages_.empty()) {
    return;
  }

  uint64_t seq = 0;
  int32_t op_type;
  {
    msg_ref_type req_msg = get_request();
    if (req_msg.has_head()) {
      seq = req_msg.head().client_sequence();
    }
    if (PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM == req_msg.head().op_type()) {
      op_type = PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM;
    } else {
      op_type = PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE;
    }
  }

  for (std::list<msg_type>::iterator iter = response_messages_.begin(); iter != response_messages_.end(); ++iter) {
    (*iter).mutable_head()->set_error_code(get_response_code());
    (*iter).mutable_head()->set_timestamp(util::time::time_utility::get_now());
    (*iter).mutable_head()->set_client_sequence(seq);
    (*iter).mutable_head()->set_op_type(op_type);

    // send message using session
    int32_t res = sess->send_msg_to_client(*iter);
    if (res) {
      if (owner_player) {
        FWPLOGERROR(*owner_player, "actor {}({}) send message failed, res: {}({})", name(),
                    reinterpret_cast<const void *>(this), res, protobuf_mini_dumper_get_error_msg(res));
      } else {
        FWLOGERROR("actor {}({}) send message to session [{:#x}, {}] failed, res: {}({})", name(),
                   reinterpret_cast<const void *>(this), sess->get_key().bus_id, sess->get_key().session_id, res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }
  }

  response_messages_.clear();
}

std::shared_ptr<dispatcher_implement> actor_action_cs_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(cs_msg_dispatcher::me());
}

const char *actor_action_cs_req_base::get_type_name() const { return "client"; }
