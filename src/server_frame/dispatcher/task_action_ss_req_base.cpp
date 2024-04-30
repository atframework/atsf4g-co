// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_ss_req_base.h"

#include <std/explicit_declare.h>

#include <log/log_wrapper.h>
#include <logic/player_manager.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <config/logic_config.h>

#include <router/router_manager_base.h>
#include <router/router_manager_set.h>
#include <router/router_object_base.h>

#include <rpc/db/uuid.h>
#include <rpc/router/routerservice.h>
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_utils.h"

SERVER_FRAME_API task_action_ss_req_base::task_action_ss_req_base(dispatcher_start_data_type &&start_param)
    : base_type(start_param) {
  // 必须先设置共享的arena
  if (nullptr != start_param.context) {
    get_shared_context().try_reuse_protobuf_arena(start_param.context->mutable_protobuf_arena());
  }

  message_type *ss_msg = ss_msg_dispatcher::me()->get_protobuf_msg<message_type>(start_param.message);
  if (nullptr != ss_msg) {
    get_request().Swap(ss_msg);

    set_user_key(get_request().head().player_user_id(), get_request().head().player_zone_id());
  }

  // 最后设置caller
  if (nullptr != start_param.context) {
    set_caller_context(*start_param.context);
  }
}

SERVER_FRAME_API task_action_ss_req_base::~task_action_ss_req_base() {}

SERVER_FRAME_API task_action_ss_req_base::result_type task_action_ss_req_base::hook_run() {
  // 路由对象系统支持
  router_manager_base *mgr = nullptr;
  std::shared_ptr<router_object_base> obj;
  if (get_request().head().has_router()) {
    auto trace_span = get_shared_context().get_trace_span();
    if (trace_span) {
      trace_span->SetAttribute("router_object.type_id", get_request().head().router().object_type_id());
      trace_span->SetAttribute("router_object.zone_id", get_request().head().router().object_zone_id());
      trace_span->SetAttribute("router_object.instance_id", get_request().head().router().object_inst_id());
    }

    std::pair<bool, int> result;
    RPC_AWAIT_IGNORE_RESULT(filter_router_msg(mgr, obj, result));
    if (false == result.first) {
      TASK_ACTION_RETURN_CODE(result.second);
    }
  }

  // 自动设置快队列保存
  result_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::hook_run());
  if (nullptr != get_dispatcher_options() && get_dispatcher_options()->mark_fast_save()) {
    if (mgr && obj) {
      router_manager_set::me()->mark_fast_save(mgr, obj);
    }
  }
  TASK_ACTION_RETURN_CODE(ret);
}

SERVER_FRAME_API uint64_t task_action_ss_req_base::get_request_node_id() const noexcept {
  msg_cref_type msg = get_request();
  if (msg.head().has_rpc_forward() && msg.head().rpc_forward().transparent()) {
    uint64_t node_id = msg.head().rpc_forward().forward_for_node_id();
    if (node_id != 0) {
      return node_id;
    }
  }
  return msg.head().node_id();
}

SERVER_FRAME_API const std::string &task_action_ss_req_base::get_request_node_name() const noexcept {
  msg_cref_type msg = get_request();
  if (msg.head().has_rpc_forward() && msg.head().rpc_forward().transparent()) {
    const std::string &node_name = msg.head().rpc_forward().forward_for_node_name();
    if (!node_name.empty()) {
      return node_name;
    }
  }

  return msg.head().node_name();
}

SERVER_FRAME_API int32_t task_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd,
                                                           gsl::string_view node_name) {
  msg.mutable_head()->set_node_id(dst_pd);
  msg.mutable_head()->set_node_name(node_name.data(), node_name.size());
  msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());

  return 0;
}

SERVER_FRAME_API int32_t task_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd,
                                                           gsl::string_view node_name, msg_cref_type req_msg) {
  init_msg(msg, dst_pd, node_name);
  auto head = msg.mutable_head();
  auto &request_head = req_msg.head();
  if (request_head.has_router()) {
    protobuf_copy_message(*head->mutable_router(), request_head.router());
  }
  if (request_head.has_rpc_trace()) {
    protobuf_copy_message(*head->mutable_rpc_trace(), request_head.rpc_trace());
  }
  if (!request_head.player_open_id().empty()) {
    head->set_player_open_id(request_head.player_open_id());
  }
  if (request_head.player_user_id() != 0) {
    head->set_player_user_id(request_head.player_user_id());
  }
  if (request_head.player_zone_id() != 0) {
    head->set_player_user_id(request_head.player_user_id());
  }

  // set task information
  if (request_head.has_rpc_forward() && request_head.rpc_forward().transparent()) {
    const atframework::RpcForward &forward_for = request_head.rpc_forward();
    if (0 != forward_for.forward_for_source_task_id()) {
      head->set_destination_task_id(forward_for.forward_for_source_task_id());
    } else {
      head->set_destination_task_id(0);
    }

    head->set_sequence(forward_for.forward_for_sequence());
  } else {
    if (0 != request_head.source_task_id()) {
      head->set_destination_task_id(request_head.source_task_id());
    } else {
      head->set_destination_task_id(0);
    }

    head->set_sequence(request_head.sequence());
  }

  if (0 != request_head.destination_task_id()) {
    head->set_source_task_id(request_head.destination_task_id());
  } else {
    head->set_source_task_id(0);
  }

  if (PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM == request_head.op_type()) {
    head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM);
  } else {
    head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  }

  return 0;
}

SERVER_FRAME_API std::shared_ptr<dispatcher_implement> task_action_ss_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
}

SERVER_FRAME_API const char *task_action_ss_req_base::get_type_name() const { return "inserver"; }

SERVER_FRAME_API rpc::context::inherit_options task_action_ss_req_base::get_inherit_option() const noexcept {
  auto &req_msg = get_request();
  if (req_msg.has_head() && req_msg.head().has_rpc_request() && 0 != req_msg.head().source_task_id()) {
    return rpc::context::inherit_options{rpc::context::parent_mode::kParent, true, false};
  }

  return rpc::context::inherit_options{rpc::context::parent_mode::kLink, true, false};
}

SERVER_FRAME_API rpc::context::trace_start_option task_action_ss_req_base::get_trace_option() const noexcept {
  rpc::context::trace_start_option ret = task_action_base::get_trace_option();

  auto &req_msg = get_request();
  if (req_msg.has_head() && req_msg.head().has_rpc_trace() && !req_msg.head().rpc_trace().trace_id().empty()) {
    ret.parent_network_span = &req_msg.head().rpc_trace();
  }

  return ret;
}

SERVER_FRAME_API bool task_action_ss_req_base::is_stream_rpc() const noexcept {
  return get_request().head().has_rpc_stream();
}

namespace {
template <class ForwardTo>
static rpc::result_code_type task_action_ss_action_forward_rpc(rpc::context &ctx, ForwardTo &&target, bool transparent,
                                                               bool &ok, bool ignore_discovery,
                                                               const atframework::SSMsg &request_message,
                                                               atframework::SSMsg &forward_request,
                                                               atframework::SSMsg &forward_response) {
  const atframework::SSMsgHead &request_head = request_message.head();
  if (request_head.has_rpc_forward() &&
      request_head.rpc_forward().ttl() >= logic_config::me()->get_logic().router().transfer_max_ttl()) {
    ok = false;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND);
  }

  protobuf_copy_message(*forward_request.mutable_head(), request_head);
  atframework::RpcForward *forward_for_head = forward_request.mutable_head()->mutable_rpc_forward();
  forward_for_head->set_transparent(transparent);
  if (request_head.has_rpc_forward() && (0 != request_head.rpc_forward().forward_for_node_id() ||
                                         !request_head.rpc_forward().forward_for_node_name().empty())) {
    forward_for_head->set_ttl(request_head.rpc_forward().ttl() + 1);
  }
  bool origin_request_transparent_forward = request_head.has_rpc_forward() && request_head.rpc_forward().transparent();
  if (origin_request_transparent_forward) {
    forward_for_head->set_forward_for_node_id(request_head.rpc_forward().forward_for_node_id());
    forward_for_head->set_forward_for_node_name(request_head.rpc_forward().forward_for_node_name());
    forward_for_head->set_forward_for_source_task_id(request_head.rpc_forward().forward_for_source_task_id());
    forward_for_head->set_forward_for_sequence(request_head.rpc_forward().forward_for_sequence());
  } else {
    forward_for_head->set_forward_for_node_id(request_head.node_id());
    forward_for_head->set_forward_for_node_name(request_head.node_name());
    forward_for_head->set_forward_for_source_task_id(request_head.source_task_id());
    forward_for_head->set_forward_for_sequence(request_head.sequence());
  }
  {
    auto forward_head = forward_request.mutable_head();
    forward_head->set_sequence(ss_msg_dispatcher::me()->allocate_sequence());
    forward_head->set_node_id(logic_config::me()->get_local_server_id());
    auto local_server_name = logic_config::me()->get_local_server_name();
    forward_head->set_node_name(local_server_name.data(), local_server_name.size());
    forward_head->set_timestamp(util::time::time_utility::get_now());
    forward_head->set_source_task_id(ctx.get_task_context().task_id);
  }
  *forward_request.mutable_body_bin() = request_message.body_bin();

  rpc::result_code_type::value_type ret =
      ss_msg_dispatcher::me()->send_to_proc(std::forward<ForwardTo>(target), forward_request, ignore_discovery);
  if (ret < 0) {
    ok = false;
    RPC_RETURN_CODE(ret);
  }

  if (!transparent) {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = forward_request.head().sequence();
    const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method =
        ss_msg_dispatcher::me()->get_registered_method(request_head.has_rpc_request()
                                                           ? request_head.rpc_request().rpc_name()
                                                           : request_head.rpc_stream().rpc_name());
    if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
      await_options.timeout = rpc::make_duration_or_default(
          method->options().GetExtension(atframework::rpc_options).timeout(),
          rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                        std::chrono::seconds{6}));
    } else {
      await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                                            std::chrono::seconds{6});
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, forward_response, await_options));
    if (ret < 0) {
      ok = false;
      RPC_RETURN_CODE(ret);
    }
  }

  ok = true;
  RPC_RETURN_CODE(ret);
}

template <class CloneTo>
static rpc::result_code_type task_action_ss_action_clone_rpc(rpc::context &ctx, CloneTo &&target, bool ignore_discovery,
                                                             const atframework::SSMsg &request_message,
                                                             atframework::SSMsg *clone_response) {
  const atframework::SSMsgHead &request_head = request_message.head();

  rpc::context::message_holder<atframework::SSMsg> clone_request{ctx};
  atframework::SSMsgHead &clone_head = *clone_request->mutable_head();

  protobuf_copy_message(clone_head, request_head);
  clone_head.set_node_id(logic_config::me()->get_local_server_id());
  auto local_server_name = logic_config::me()->get_local_server_name();
  clone_head.set_node_name(local_server_name.data(), local_server_name.size());
  clone_head.set_source_task_id(ctx.get_task_context().task_id);
  clone_head.set_sequence(ss_msg_dispatcher::me()->allocate_sequence());

  // Clone 请求要清理转发逻辑
  clone_head.clear_rpc_forward();

  bool need_wait_response = request_head.has_rpc_request() && nullptr != clone_response;
  // 不需要回包则转为stream消息
  if (!need_wait_response) {
    if (clone_head.has_rpc_request()) {
      std::string rpc_version;
      std::string rpc_caller;
      std::string rpc_callee;
      std::string rpc_rpc_name;
      std::string rpc_type_url;
      clone_head.mutable_rpc_request()->mutable_version()->swap(rpc_version);
      clone_head.mutable_rpc_request()->mutable_caller()->swap(rpc_caller);
      clone_head.mutable_rpc_request()->mutable_callee()->swap(rpc_callee);
      clone_head.mutable_rpc_request()->mutable_rpc_name()->swap(rpc_rpc_name);
      clone_head.mutable_rpc_request()->mutable_type_url()->swap(rpc_type_url);
      clone_head.clear_rpc_request();
      clone_head.mutable_rpc_stream()->mutable_version()->swap(rpc_version);
      clone_head.mutable_rpc_stream()->mutable_caller()->swap(rpc_caller);
      clone_head.mutable_rpc_stream()->mutable_callee()->swap(rpc_callee);
      clone_head.mutable_rpc_stream()->mutable_rpc_name()->swap(rpc_rpc_name);
      clone_head.mutable_rpc_stream()->mutable_type_url()->swap(rpc_type_url);
    }
  }
  *clone_request->mutable_body_bin() = request_message.body_bin();

  rpc::result_code_type::value_type ret =
      ss_msg_dispatcher::me()->send_to_proc(std::forward<CloneTo>(target), *clone_request, ignore_discovery);
  if (ret < 0 || !need_wait_response) {
    RPC_RETURN_CODE(ret);
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = clone_request->head().sequence();
  const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = ss_msg_dispatcher::me()->get_registered_method(
      request_head.has_rpc_request() ? request_head.rpc_request().rpc_name() : request_head.rpc_stream().rpc_name());
  if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
    await_options.timeout = rpc::make_duration_or_default(
        method->options().GetExtension(atframework::rpc_options).timeout(),
        rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                      std::chrono::seconds{6}));
  } else {
    await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                                          std::chrono::seconds{6});
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, *clone_response, await_options));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(ret);
}
}  // namespace

SERVER_FRAME_API rpc::result_code_type task_action_ss_req_base::forward_rpc(const atapp::etcd_discovery_node &node,
                                                                            bool transparent, bool &ok,
                                                                            bool ignore_discovery) {
  if (has_response_message()) {
    ok = false;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_ALREADY_HAS_RESPONSE);
  }

  rpc::context::message_holder<atframework::SSMsg> forward_request{get_shared_context()};
  rpc::context::message_holder<atframework::SSMsg> forward_response{get_shared_context()};

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(
      task_action_ss_action_forward_rpc(get_shared_context(), node, transparent, ok, ignore_discovery, get_request(),
                                        *forward_request, *forward_response));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  if (ok) {
    if (transparent) {
      disable_response_message();
    } else if (!is_stream_rpc() && !has_response_message() && is_response_message_enabled()) {
      atframework::SSMsg &response_message = add_response_message();
      atframework::SSMsgHead *head = response_message.mutable_head();
      head->set_error_code(forward_response->head().error_code());
      if (0 != forward_response->head().external_error_code()) {
        head->set_external_error_code(forward_response->head().external_error_code());
      }
      if (!forward_response->head().external_error_message().empty()) {
        head->set_external_error_message(forward_response->head().external_error_message());
      }
      head->set_player_user_id(forward_response->head().player_user_id());
      head->set_player_open_id(forward_response->head().player_open_id());
      head->set_player_zone_id(forward_response->head().player_zone_id());

      // Swap body
      response_message.mutable_body_bin()->swap(*forward_response->mutable_body_bin());
    }
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type task_action_ss_req_base::forward_rpc(uint64_t node_id, bool transparent,
                                                                            bool &ok, bool ignore_discovery) {
  if (has_response_message()) {
    ok = false;
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_ALREADY_HAS_RESPONSE);
  }

  rpc::context::message_holder<atframework::SSMsg> forward_request{get_shared_context()};
  rpc::context::message_holder<atframework::SSMsg> forward_response{get_shared_context()};

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(
      task_action_ss_action_forward_rpc(get_shared_context(), node_id, transparent, ok, ignore_discovery, get_request(),
                                        *forward_request, *forward_response));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  if (ok) {
    if (transparent) {
      disable_response_message();
    } else if (!is_stream_rpc() && !has_response_message() && is_response_message_enabled()) {
      atframework::SSMsg &response_message = add_response_message();
      atframework::SSMsgHead *head = response_message.mutable_head();
      head->set_error_code(forward_response->head().error_code());
      if (0 != forward_response->head().external_error_code()) {
        head->set_external_error_code(forward_response->head().external_error_code());
      }
      if (!forward_response->head().external_error_message().empty()) {
        head->set_external_error_message(forward_response->head().external_error_message());
      }
      head->set_player_user_id(forward_response->head().player_user_id());
      head->set_player_open_id(forward_response->head().player_open_id());
      head->set_player_zone_id(forward_response->head().player_zone_id());

      // Swap body
      response_message.mutable_body_bin()->swap(*forward_response->mutable_body_bin());
    }
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type task_action_ss_req_base::clone_rpc(const atapp::etcd_discovery_node &node,
                                                                          atframework::SSMsg *response_message,
                                                                          bool ignore_discovery) {
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(
      task_action_ss_action_clone_rpc(get_shared_context(), node, ignore_discovery, get_request(), response_message));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type task_action_ss_req_base::clone_rpc(uint64_t node_id,
                                                                          atframework::SSMsg *response_message,
                                                                          bool ignore_discovery) {
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(task_action_ss_action_clone_rpc(
      get_shared_context(), node_id, ignore_discovery, get_request(), response_message));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API atframework::SSMsg &task_action_ss_req_base::add_response_message() {
  message_type *msg = get_shared_context().create<message_type>();
  if (nullptr == msg) {
    static message_type empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  response_messages_.push_back(msg);

  atframework::SSMsgHead *head = msg->mutable_head();
  if (get_request().head().has_rpc_request()) {
    head->clear_rpc_request();
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

  return *msg;
}

SERVER_FRAME_API void task_action_ss_req_base::send_response() {
  if (response_messages_.empty()) {
    return;
  }

  for (std::list<message_type *>::iterator iter = response_messages_.begin(); iter != response_messages_.end();
       ++iter) {
    if (0 == (*iter)->head().node_id() && (*iter)->head().node_name().empty()) {
      FCTXLOGERROR(get_shared_context(), "{}", "send message to unknown server");
      continue;
    }

    atframework::SSMsgHead *head = (*iter)->mutable_head();
    head->set_error_code(get_response_code());
    if (0 != get_external_response_code()) {
      head->set_external_error_code(get_response_code());
    }
    if (!get_external_response_message().empty()) {
      head->set_external_error_message(get_external_response_message());
    }

    // send message using ss dispatcher
    int32_t res;
    if (0 != head->node_id()) {
      res = ss_msg_dispatcher::me()->send_to_proc(head->node_id(), **iter, true);
    } else {
      res = ss_msg_dispatcher::me()->send_to_proc(head->node_name(), **iter, true);
    }
    if (res) {
      FCTXLOGERROR(get_shared_context(), "send message to server {:#x} failed, res: {}({})", head->node_id(), res,
                   protobuf_mini_dumper_get_error_msg(res));
    }
  }

  response_messages_.clear();
}

namespace detail {
struct filter_router_message_result_type {
  bool is_on_current_server;
  bool enable_retry;

  inline filter_router_message_result_type() : is_on_current_server(false), enable_retry(false) {}
  inline filter_router_message_result_type(bool cur, bool retry) : is_on_current_server(cur), enable_retry(retry) {}
};

static rpc::result_code_type try_fetch_router_cache(rpc::context &ctx, router_manager_base &mgr,
                                                    router_manager_base::key_t key,
                                                    std::shared_ptr<router_object_base> &obj) {
  rpc::result_code_type::value_type res = 0;
  obj = mgr.get_base_cache(key);

  // 如果不存在那么实体一定不在这台机器上，但是可能在其他机器上，需要拉取一次确认
  if (!obj) {
    if (!mgr.is_auto_mutable_cache()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
    }
    res = RPC_AWAIT_CODE_RESULT(mgr.mutable_cache(ctx, obj, key, nullptr));
    if (res < 0 || !obj) {
      FCTXLOGERROR(ctx, "router object {}:{}:{} fetch cache failed, res: {}({})", key.type_id, key.zone_id,
                   key.object_id, res, protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_CODE(res);
    }
  } else {
    res = RPC_AWAIT_CODE_RESULT(obj->await_io_task(ctx));
    if (res < 0 || !obj) {
      FCTXLOGERROR(ctx, "router object {}:{}:{} await_io_task, res: {}({})", key.type_id, key.zone_id, key.object_id,
                   res, protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_CODE(res);
    }
  }

  RPC_RETURN_CODE(res);
}

static rpc::result_code_type auto_mutable_router_object(rpc::context &ctx, uint64_t self_node_id,
                                                        router_manager_base &mgr, router_manager_base::key_t key,
                                                        std::shared_ptr<router_object_base> &obj,
                                                        filter_router_message_result_type &result) {
  // 如果开启了自动拉取object，尝试拉取object
  if (!mgr.is_auto_mutable_object()) {
    FCTXLOGINFO(ctx, "router object key={}:{}:{} not found and not auto mutable object", key.type_id, key.zone_id,
                key.object_id);
    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
  }

  auto res = RPC_AWAIT_CODE_RESULT(mgr.mutable_object(ctx, obj, key, nullptr));
  if (res < 0) {
    FCTXLOGERROR(ctx, "router object {}:{}:{} repair object failed, res: {}({})", key.type_id, key.zone_id,
                 key.object_id, res, protobuf_mini_dumper_get_error_msg(res));
    // 失败则删除缓存重试
    RPC_AWAIT_IGNORE_RESULT(mgr.remove_cache(ctx, key, obj, nullptr));

    result = filter_router_message_result_type(false, true);
    RPC_RETURN_CODE(res);
  }

  // Check log
  if (self_node_id != obj->get_router_server_id()) {
    FCTXLOGERROR(ctx,
                 "router object {}:{}:{} auto mutable object failed, expect server id 0x{:x}, real server id 0x{:x}",
                 key.type_id, key.zone_id, key.object_id, self_node_id, obj->get_router_server_id());
    if (0 == obj->get_router_server_id()) {
      result = filter_router_message_result_type(false, true);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
    } else {
      result = filter_router_message_result_type(false, false);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_IN_OTHER_SERVER);
    }
  }

  result = filter_router_message_result_type(true, true);
  RPC_RETURN_CODE(res);
}

static rpc::result_code_type check_local_router_object(rpc::context &ctx, uint64_t self_node_id,
                                                       router_manager_base &mgr, router_manager_base::key_t key,
                                                       std::shared_ptr<router_object_base> &obj,
                                                       filter_router_message_result_type &result) {
  // 路由对象命中当前节点，要开始执行任务逻辑
  if (obj->is_writable()) {
    result = filter_router_message_result_type(true, true);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 这里可能是服务器崩溃过，导致数据库记录对象在本机上，但实际上没有。所以这里升级一次做个数据修复
  auto res = RPC_AWAIT_CODE_RESULT(mgr.mutable_object(ctx, obj, key, nullptr));
  if (res < 0) {
    FCTXLOGERROR(ctx, "router object {}:{}:{} repair object failed, res: {}({})", key.type_id, key.zone_id,
                 key.object_id, res, protobuf_mini_dumper_get_error_msg(res));
    // 失败则删除缓存重试
    RPC_AWAIT_IGNORE_RESULT(mgr.remove_cache(ctx, key, obj, nullptr));

    result = filter_router_message_result_type(false, true);
    RPC_RETURN_CODE(res);
  }

  // Check log
  if (self_node_id != obj->get_router_server_id()) {
    FCTXLOGERROR(ctx, "router object {}:{}:{} repair object failed, expect server id 0x{:x}, real server id 0x{:x}",
                 key.type_id, key.zone_id, key.object_id, self_node_id, obj->get_router_server_id());
  }

  // 恢复成功，直接开始执行任务逻辑
  result = filter_router_message_result_type(true, true);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

static rpc::result_code_type try_filter_router_msg(rpc::context &ctx, EXPLICIT_UNUSED_ATTR int retry_times,
                                                   uint64_t /*request_node_id*/, atframework::SSMsg &request_msg,
                                                   router_manager_base &mgr, router_manager_base::key_t key,
                                                   std::shared_ptr<router_object_base> &obj,
                                                   filter_router_message_result_type &result) {
  obj.reset();

  const atframework::SSRouterHead &router = request_msg.head().router();
  int32_t res = RPC_AWAIT_CODE_RESULT(try_fetch_router_cache(ctx, mgr, key, obj));
  if (res == PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND) {
    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(res);
  }

  if (!obj) {
    if (res >= 0) {
      res = PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND;
    }
    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(res);
  } else if (res < 0) {
    result = filter_router_message_result_type(obj->is_writable(), true);
    RPC_RETURN_CODE(res);
  }

  // 如果正在迁移，追加到pending队列，本task直接退出
  if (obj->check_flag(router_object_base::flag_t::EN_ROFT_TRANSFERING)) {
    obj->get_transfer_pending_list().push_back(atframework::SSMsg());
    obj->get_transfer_pending_list().back().Swap(&request_msg);

    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 如果本地版本号低于来源服务器，刷新一次路由表。正常情况下这里不可能走到，如果走到了。需要删除缓存再来一次
  if (obj->get_router_version() < router.router_version()) {
    if (obj->is_writable()) {
      FCTXLOGERROR(ctx, "router object {}:{}:{} has invalid router version, refresh cache. local: {}, remote: {}",
                   key.type_id, key.zone_id, key.object_id, obj->get_router_version(), router.router_version());
    } else {
      FCTXLOGINFO(ctx, "router object {}:{}:{} reroute object, refresh cache. local: {}, remote: {}", key.type_id,
                  key.zone_id, key.object_id, obj->get_router_version(), router.router_version());
    }
    RPC_AWAIT_IGNORE_RESULT(mgr.remove_cache(ctx, key, obj, nullptr));
    result = filter_router_message_result_type(false, true);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  uint64_t self_node_id = logic_config::me()->get_local_server_id();
  // 可能本地缓存的路由信息过期，路由节点返回0的话说明最后一次登记时对象离线了，这时候只能尝试去数据库获取一次新的信息
  if (0 == obj->get_router_server_id() && !mgr.is_auto_mutable_object() && !obj->is_writable()) {
    uint64_t renew_router_server_id = 0;
    uint64_t renew_router_version = 0;
    res = RPC_AWAIT_CODE_RESULT(mgr.pull_online_server(ctx, key, renew_router_server_id, renew_router_version));
    if (res < 0) {
      result = filter_router_message_result_type(false, true);
      RPC_RETURN_CODE(res);
    }

    if (0 == renew_router_server_id) {
      result = filter_router_message_result_type(false, false);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
    }

    if (!obj->is_writable() && 0 == obj->get_router_server_id() && obj->get_router_server_name().empty() &&
        renew_router_version > obj->get_router_version()) {
      obj->set_router_server_id(renew_router_server_id, renew_router_version);
    }
  }

  if (0 == obj->get_router_server_id()) {
    filter_router_message_result_type auto_res;
    res = RPC_AWAIT_CODE_RESULT(auto_mutable_router_object(ctx, self_node_id, mgr, key, obj, auto_res));
    if (res < 0) {
      result = auto_res;
      RPC_RETURN_CODE(res);
    }
  }

  // 如果和本地的路由缓存匹配则break直接开始消息处理
  if (obj && self_node_id == obj->get_router_server_id()) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(check_local_router_object(ctx, self_node_id, mgr, key, obj, result)));
  }

  // 路由消息转发
  if (obj && 0 != obj->get_router_server_id()) {
    uint64_t rpc_sequence;
    if (request_msg.head().router().router_transfer_ttl() < logic_config::me()->get_cfg_router().transfer_max_ttl()) {
      request_msg.mutable_head()->mutable_router()->set_router_transfer_ttl(
          request_msg.head().router().router_transfer_ttl() + 1);
      res = RPC_AWAIT_CODE_RESULT(mgr.send_msg(ctx, *obj, std::move(request_msg), rpc_sequence));
    } else {
      res = PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND;
    }

    // 如果路由转发成功，需要禁用掉回包和通知事件，也不需要走逻辑处理了
    if (res < 0) {
      FCTXLOGERROR(ctx, "try to transfer router object {}:{}:{} to 0x{:x} failed, res: {}({})", key.type_id,
                   key.zone_id, key.object_id, obj->get_router_server_id(), res,
                   protobuf_mini_dumper_get_error_msg(res));
    }

    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(res);
  }

  // 这个分支理论上也不会跑到，前面已经枚举了所有流程分支了
  FCTXLOGERROR(ctx, "miss router object {}:{}:{} prediction code", key.type_id, key.zone_id, key.object_id);
  result = filter_router_message_result_type(false, true);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
}
}  // namespace detail

SERVER_FRAME_API rpc::result_code_type task_action_ss_req_base::filter_router_msg(
    router_manager_base *&mgr, std::shared_ptr<router_object_base> &obj, std::pair<bool, int> &filter_result) {
  // request 可能会被move走，所以这里copy一份
  atframework::SSRouterHead router;
  protobuf_copy_message(router, get_request().head().router());

  // find router manager in router set
  mgr = router_manager_set::me()->get_manager(router.object_type_id());
  if (nullptr == mgr) {
    FCTXLOGERROR(get_shared_context(), "router manager {} not found", router.object_type_id());
    filter_result = std::make_pair(false, PROJECT_NAMESPACE_ID::err::EN_ROUTER_TYPE_INVALID);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TYPE_INVALID);
  }

  router_manager_base::key_t key(router.object_type_id(), router.object_zone_id(), router.object_inst_id());
  router_object_base::trace_router(get_shared_context(), key);

  int retry_times = 0;
  rpc::result_code_type::value_type last_result = 0;

  // 最多重试3次，故障恢复过程中可能发生抢占，这时候正常情况下第二次就应该会成功
  while ((++retry_times) <= 3) {
    detail::filter_router_message_result_type internal_filter_result;
    last_result =
        RPC_AWAIT_CODE_RESULT(detail::try_filter_router_msg(get_shared_context(), retry_times, get_request_node_id(),
                                                            get_request(), *mgr, key, obj, internal_filter_result));
    if (internal_filter_result.is_on_current_server) {
      filter_result = std::make_pair(true, last_result);
      RPC_RETURN_CODE(last_result);
    }

    // 如果路由转发成功或者路由转移期间待处理的消息队列添加成功
    // 需要禁用掉回包和通知事件，也不需要走逻辑处理了
    if (last_result >= 0) {
      disable_response_message();
      disable_finish_event();
      break;
    }

    // 某些情况下不需要重试
    if (!internal_filter_result.enable_retry) {
      break;
    }
  }

  // 如果本地路由版本号大于来源，通知来源更新路由表
  if (last_result >= 0 && obj && obj->get_router_version() > router.router_version()) {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRouterUpdateSync> sync_msg{get_shared_context()};
    atframework::SSRouterHead *router_head = sync_msg->mutable_object();
    if (nullptr != router_head) {
      router_head->set_router_source_node_id(obj->get_router_server_id());
      router_head->set_router_source_node_name(obj->get_router_server_name());
      router_head->set_router_version(obj->get_router_version());
      router_head->set_object_type_id(key.type_id);
      router_head->set_object_inst_id(key.object_id);
      router_head->set_object_zone_id(key.zone_id);
    }

    // 只通知直接来源
    RPC_AWAIT_IGNORE_RESULT(rpc::router::router_update_sync(get_shared_context(), get_request_node_id(), *sync_msg));
  }

  // 失败则要回发转发失败
  set_response_code(last_result);

  // 如果忽略路由节点不在线,直接返回0即可
  if (PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER == last_result && is_router_offline_ignored()) {
    last_result = 0;
  }

  if (obj && last_result < 0) {
    obj->send_transfer_msg_failed(std::move(get_request()));
  }
  filter_result = std::make_pair(false, last_result);
  RPC_RETURN_CODE(last_result);
}

SERVER_FRAME_API bool task_action_ss_req_base::is_router_offline_ignored() const { return false; }
