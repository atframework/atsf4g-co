// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_cs_req_base.h"

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <data/player_cache.h>
#include <logic/player_manager.h>
#include <logic/session_manager.h>

// #include <router/router_player_cache.h>
#include <router/router_player_manager.h>

#include <dispatcher/cs_msg_dispatcher.h>

#include <router/router_manager_base.h>
#include <router/router_manager_set.h>
#include <router/router_object_base.h>

#include <rpc/db/uuid.h>

#include <list>
#include <memory>
#include <utility>

task_action_cs_req_base::task_action_cs_req_base(dispatcher_start_data_t &&start_param)
    : has_sync_dirty_(false), recursive_sync_dirty_(false) {
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

task_action_cs_req_base::~task_action_cs_req_base() {}

task_action_cs_req_base::result_type task_action_cs_req_base::hook_run() {
  std::shared_ptr<player_cache> player_cache = get_player_cache();
  if (player_cache) {
    player_cache->refresh_feature_limit(get_shared_context());
  }

  int ret = base_type::hook_run();

  // 自动设置快队列保存
  do {
    auto dispatcher_options = get_dispatcher_start_data().options;
    if (nullptr == dispatcher_options) {
      break;
    }

    if (!dispatcher_options->mark_fast_save() && !dispatcher_options->mark_wait_save()) {
      break;
    }

    router_manager_base *mgr = router_manager_set::me()->get_manager(PROJECT_NAMESPACE_ID::EN_ROT_PLAYER);
    if (nullptr == mgr) {
      break;
    }

    if (!player_cache) {
      player_cache = get_player_cache();
      if (player_cache) {
        player_cache->refresh_feature_limit(get_shared_context());
      }
    }

    if (!player_cache) {
      break;
    }

    router_player_manager::ptr_t obj = router_player_manager::me()->get_cache(router_player_manager::key_t(
        router_player_manager::me()->get_type_id(), player_cache->get_zone_id(), player_cache->get_user_id()));
    if (!obj || !obj->is_writable() || !obj->is_object_equal(player_cache)) {
      break;
    }
    obj->trace_router(get_shared_context());

    if (dispatcher_options->mark_wait_save()) {
      ret = RPC_AWAIT_CODE_RESULT(player_manager::me()->save(get_shared_context(), player_cache->get_user_id(),
                                                             player_cache->get_zone_id(), player_cache.get()));
      if (ret < 0) {
        FWLOGERROR("save player {}:{} failed, res: {}({})", player_cache->get_zone_id(), player_cache->get_user_id(),
                   ret, protobuf_mini_dumper_get_error_msg(ret));
      }
    } else {
      router_manager_set::me()->mark_fast_save(mgr, obj);
    }
  } while (false);

  return ret;
}

std::shared_ptr<dispatcher_implement> task_action_cs_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(cs_msg_dispatcher::me());
}

const char *task_action_cs_req_base::get_type_name() const { return "client"; }

std::pair<uint64_t, uint64_t> task_action_cs_req_base::get_gateway_info() const {
  const msg_type &cs_msg = get_request();
  return std::pair<uint64_t, uint64_t>(cs_msg.head().session_bus_id(), cs_msg.head().session_id());
}

session::ptr_t task_action_cs_req_base::get_session() const {
  if (session_inst_) {
    return session_inst_;
  }

  session::key_t key(get_gateway_info());
  session_inst_ = session_manager::me()->find(key);
  return session_inst_;
}

std::shared_ptr<player_cache> task_action_cs_req_base::get_player_cache() const {
  std::shared_ptr<session> sess = get_session();
  if (!sess) {
    return nullptr;
  }

  return sess->get_player();
}

task_action_cs_req_base::msg_ref_type task_action_cs_req_base::add_rsp_msg() {
  msg_type *msg = get_shared_context().create<msg_type>();
  if (nullptr == msg) {
    static msg_type empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  response_messages_.push_back(msg);
  return *msg;
}

std::list<task_action_cs_req_base::msg_type *> &task_action_cs_req_base::get_rsp_list() { return response_messages_; }

const std::list<task_action_cs_req_base::msg_type *> &task_action_cs_req_base::get_rsp_list() const {
  return response_messages_;
}

void task_action_cs_req_base::send_response() {
  if (!has_sync_dirty_) {
    send_response(true);
    return;
  }

  if (response_messages_.empty()) {
    return;
  }

  session::ptr_t sess = get_session();
  if (!sess) {
    std::pair<uint64_t, uint64_t> sess_id = get_gateway_info();
    FWLOGWARNING("try to send response message, but session [{:#x}, {}] not found", sess_id.first, sess_id.second);
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

  for (std::list<msg_type *>::iterator iter = response_messages_.begin(); iter != response_messages_.end(); ++iter) {
    (*iter)->mutable_head()->set_error_code(get_response_code());
    (*iter)->mutable_head()->set_timestamp(util::time::time_utility::get_now());
    (*iter)->mutable_head()->set_client_sequence(seq);
    if ((*iter)->head().op_type() == PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP) {
      (*iter)->mutable_head()->set_op_type(op_type);
    }

    // send message using session
    int32_t res = sess->send_msg_to_client(**iter);
    if (res) {
      if (get_user_id() != 0) {
        FWLOGERROR("task {} [{}] send message to player_cache {}:{} failed, res: {}", name(), get_task_id(),
                   get_zone_id(), get_user_id(), res);
      } else {
        FWLOGERROR("task {} [{}] send message to session [{:#x}, {}] failed, res: {}", name(), get_task_id(),
                   sess->get_key().bus_id, sess->get_key().session_id, res);
      }
    }
  }

  response_messages_.clear();
}

void task_action_cs_req_base::send_response(bool sync_dirty) {
  if (recursive_sync_dirty_) {
    return;
  }
  recursive_sync_dirty_ = true;
  auto recursive_sync_dirty_guard = gsl::finally([this]() { recursive_sync_dirty_ = false; });

  do {
    if (has_sync_dirty_ || !sync_dirty) {
      break;
    }
    has_sync_dirty_ = true;

    player_cache::ptr_t owner_player = get_player_cache();
    if (!owner_player) {
      FWLOGWARNING("session may be invalid now, ignore sync message for user {}:{}", get_zone_id(), get_user_id());
      break;
    }

    owner_player->send_all_syn_msg(get_shared_context());

    // refresh visit time if success
    if (0 == get_response_code()) {
      router_player_manager::ptr_t router_cache = router_player_manager::me()->get_cache(router_player_manager::key_t(
          router_player_manager::me()->get_type_id(), owner_player->get_zone_id(), owner_player->get_user_id()));
      if (router_cache && router_cache->is_object_equal(owner_player)) {
        router_cache->refresh_visit_time();
      }
    }
  } while (false);

  send_response();
}

void task_action_cs_req_base::write_actor_log_body(const google::protobuf::Message &msg,
                                                   const atframework::CSMsgHead &head) {
  auto sess = get_session();
  if (sess) {
    sess->write_actor_log_body(msg, head);
  }
}
