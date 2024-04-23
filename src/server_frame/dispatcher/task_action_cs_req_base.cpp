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

SERVER_FRAME_API task_action_cs_req_base::task_action_cs_req_base(dispatcher_start_data_type &&start_param)
    : base_type(start_param), has_sync_dirty_(false), recursive_sync_dirty_(false) {
  // 必须先设置共享的arena
  if (nullptr != start_param.context) {
    get_shared_context().try_reuse_protobuf_arena(start_param.context->mutable_protobuf_arena());
  }

  message_type *cs_msg = cs_msg_dispatcher::me()->get_protobuf_msg<message_type>(start_param.message);
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

SERVER_FRAME_API task_action_cs_req_base::~task_action_cs_req_base() {}

SERVER_FRAME_API task_action_cs_req_base::result_type task_action_cs_req_base::hook_run() {
  std::shared_ptr<player_cache> player_cache = get_player_cache();
  /**
  do {
    std::shared_ptr<session> sess = get_session();
    if (!sess) {
      break;
    }
    rpc::telemetry::tracer::span_ptr_type trace_span = get_shared_context().get_trace_span();
    if (!trace_span) {
      break;
    }

    trace_span->SetAttribute("client.address", sess->get_client_ip());
    trace_span->SetAttribute("client.port", sess->get_client_port());
  } while (false);
  **/

  router_player_manager::ptr_t router_obj;
  do {
    if (player_cache == nullptr) {
      break;
    }

    player_cache->refresh_feature_limit(get_shared_context());

    router_obj = router_player_manager::me()->get_cache(router_player_manager::key_t(
        router_player_manager::me()->get_type_id(), player_cache->get_zone_id(), player_cache->get_user_id()));
    if (router_obj && (!router_obj->is_writable() || !router_obj->is_object_equal(player_cache))) {
      router_obj.reset();
    } else {
      router_obj->trace_router(get_shared_context());
    }
  } while (false);

  // 用户层消息过滤
  do {
    if (player_cache == nullptr) {
      break;
    }

    int32_t result = player_cache->client_rpc_filter(get_shared_context(), *this, get_dispatcher_options());
    if (result < 0) {
      write_actor_log_head();
      set_response_code(result);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
    }
  } while (false);

  result_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::hook_run());

  // 自动设置快队列保存
  do {
    auto dispatcher_options = get_dispatcher_options();
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

    if (!player_cache || !router_obj) {
      break;
    }

    if (dispatcher_options->mark_wait_save()) {
      ret = RPC_AWAIT_CODE_RESULT(player_manager::me()->save(get_shared_context(), player_cache->get_user_id(),
                                                             player_cache->get_zone_id(), player_cache.get()));
      if (ret < 0) {
        FWLOGERROR("save player {}:{} failed, res: {}({})", player_cache->get_zone_id(), player_cache->get_user_id(),
                   ret, protobuf_mini_dumper_get_error_msg(ret));
      }
    } else {
      router_manager_set::me()->mark_fast_save(mgr, router_obj);
    }
  } while (false);

  TASK_ACTION_RETURN_CODE(ret);
}

SERVER_FRAME_API std::shared_ptr<dispatcher_implement> task_action_cs_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(cs_msg_dispatcher::me());
}

SERVER_FRAME_API const char *task_action_cs_req_base::get_type_name() const { return "client"; }

SERVER_FRAME_API rpc::context::trace_start_option task_action_cs_req_base::get_trace_option() const noexcept {
  rpc::context::trace_start_option ret = task_action_base::get_trace_option();

  auto &req_msg = get_request();
  if (req_msg.has_head() && req_msg.head().has_rpc_trace() && !req_msg.head().rpc_trace().trace_id().empty()) {
    ret.parent_network_span = &req_msg.head().rpc_trace();
  }

  return ret;
}

SERVER_FRAME_API bool task_action_cs_req_base::is_stream_rpc() const noexcept {
  return get_request().head().has_rpc_stream();
}

SERVER_FRAME_API std::pair<uint64_t, uint64_t> task_action_cs_req_base::get_gateway_info() const {
  const message_type &cs_msg = get_request();
  return std::pair<uint64_t, uint64_t>(cs_msg.head().session_node_id(), cs_msg.head().session_id());
}

SERVER_FRAME_API session::ptr_t task_action_cs_req_base::get_session() const {
  if (session_inst_) {
    return session_inst_;
  }

  session::key_t key(get_gateway_info());
  session_inst_ = session_manager::me()->find(key);
  return session_inst_;
}

SERVER_FRAME_API std::shared_ptr<player_cache> task_action_cs_req_base::get_player_cache() const {
  std::shared_ptr<session> sess = get_session();
  if (!sess) {
    return nullptr;
  }

  return sess->get_player();
}

SERVER_FRAME_API task_action_cs_req_base::msg_ref_type task_action_cs_req_base::add_response_message() {
  message_type *msg = get_shared_context().create<message_type>();
  if (nullptr == msg) {
    static message_type empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  atframework::CSMsgHead *head = msg->mutable_head();

  head->set_error_code(get_response_code());
  head->set_timestamp(util::time::time_utility::get_now());
  head->set_client_sequence(get_request().head().client_sequence());
  if (get_request().head().op_type() == PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM) {
    head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM);
  } else {
    head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  }

  if (get_request().head().has_rpc_request()) {
    head->mutable_rpc_response()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
    head->mutable_rpc_response()->set_rpc_name(get_request().head().rpc_request().rpc_name());
    head->mutable_rpc_response()->set_type_url(get_response_type_url());
  } else {
    head->clear_rpc_stream();
    head->mutable_rpc_stream()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
    head->mutable_rpc_stream()->set_rpc_name(get_request().head().rpc_stream().rpc_name());
    head->mutable_rpc_stream()->set_type_url(get_response_type_url());

    head->mutable_rpc_stream()->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
    head->mutable_rpc_stream()->set_callee(get_request().head().rpc_stream().caller());
  }

  response_messages_.push_back(msg);
  return *msg;
}

SERVER_FRAME_API std::list<task_action_cs_req_base::message_type *> &task_action_cs_req_base::get_rsp_list() {
  return response_messages_;
}

SERVER_FRAME_API const std::list<task_action_cs_req_base::message_type *> &task_action_cs_req_base::get_rsp_list()
    const {
  return response_messages_;
}

SERVER_FRAME_API void task_action_cs_req_base::write_actor_log_head() {
  std::shared_ptr<session> sess = get_session();
  if (sess) {
    google::protobuf::Empty empty;
    sess->write_actor_log_body(get_shared_context(), empty, get_request().head(), true);
  }
}

SERVER_FRAME_API void task_action_cs_req_base::send_response() {
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

  for (std::list<message_type *>::iterator iter = response_messages_.begin(); iter != response_messages_.end();
       ++iter) {
    (*iter)->mutable_head()->set_error_code(get_response_code());

    // send message using session
    int32_t res = sess->send_msg_to_client(get_shared_context(), **iter);
    if (res) {
      if (get_user_id() != 0) {
        FWLOGERROR("task {} [{}] send message to player_cache {}:{} failed, res: {}", name(), get_task_id(),
                   get_zone_id(), get_user_id(), res);
      } else {
        FWLOGERROR("task {} [{}] send message to session [{:#x}, {}] failed, res: {}", name(), get_task_id(),
                   sess->get_key().node_id, sess->get_key().session_id, res);
      }
    }
  }

  response_messages_.clear();
}

SERVER_FRAME_API void task_action_cs_req_base::send_response(bool sync_dirty) {
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

SERVER_FRAME_API void task_action_cs_req_base::write_actor_log_body(const google::protobuf::Message &msg,
                                                                    const atframework::CSMsgHead &head, bool is_input) {
  auto sess = get_session();
  if (sess) {
    sess->write_actor_log_body(get_shared_context(), msg, head, is_input);
  }
}
