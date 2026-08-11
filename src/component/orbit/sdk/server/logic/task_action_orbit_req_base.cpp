#include "logic/task_action_orbit_req_base.h"

#include <std/explicit_declare.h>

#include <time/time_utility.h>

#include <logic/orbit_msg_dispatcher.h>
#include <rpc/rpc_context.h>

ORBIT_SERVER_SERVICE_API task_action_orbit_req_base::task_action_orbit_req_base(
    dispatcher_start_data_type &&start_param)
    : base_type(start_param), client_id_(orbit_msg_dispatcher::get_source_client_id(start_param.private_data)) {
  if (nullptr != start_param.context) {
    get_shared_context().try_reuse_protobuf_arena(start_param.context->mutable_protobuf_arena());
  }

  message_type *orbit_msg = orbit_msg_dispatcher::me()->get_protobuf_msg<message_type>(start_param.message);
  if (nullptr != orbit_msg) {
    get_request().Swap(orbit_msg);
  }

  if (nullptr != start_param.context) {
    set_caller_context(*start_param.context);
  }
}

ORBIT_SERVER_SERVICE_API task_action_orbit_req_base::~task_action_orbit_req_base() {}

ORBIT_SERVER_SERVICE_API task_action_orbit_req_base::result_type task_action_orbit_req_base::hook_run() {
  result_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::hook_run());
  TASK_ACTION_RETURN_CODE(ret);
}

ORBIT_SERVER_SERVICE_API task_action_orbit_req_base::msg_ref_type task_action_orbit_req_base::add_response_message() {
  message_type *msg = get_shared_context().create<message_type>();
  if (nullptr == msg) {
    static message_type empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  init_msg(*msg, get_request());

  atfw::orbit::OrbitRpcMessageHead *head = msg->mutable_head();
  if (nullptr == head) {
    static message_type empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  if (get_request().head().has_rpc_request()) {
    auto rpc_response = head->mutable_rpc_response();
    if (nullptr != rpc_response) {
      auto response_type_url = get_response_type_url();
      rpc_response->set_version(logic_config::me()->get_atframework_settings().rpc_version());
      rpc_response->set_rpc_name(get_request().head().rpc_request().rpc_name());
      rpc_response->set_type_url(response_type_url.data(), response_type_url.size());
      rpc_response->set_caller_node_id(logic_config::me()->get_local_server_id());
      rpc_response->set_caller_node_name(static_cast<std::string>(logic_config::me()->get_local_server_name()));
      protobuf_copy_message(*rpc_response->mutable_caller_timestamp(),
                            get_request().head().rpc_request().caller_timestamp());
    }
  } else if (get_request().head().has_rpc_stream()) {
    auto rpc_stream = head->mutable_rpc_stream();
    if (nullptr != rpc_stream) {
      auto response_type_url = get_response_type_url();
      rpc_stream->set_version(logic_config::me()->get_atframework_settings().rpc_version());
      rpc_stream->set_rpc_name(get_request().head().rpc_stream().rpc_name());
      rpc_stream->set_type_url(response_type_url.data(), response_type_url.size());
      rpc_stream->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
      rpc_stream->set_callee(get_request().head().rpc_stream().caller());
      protobuf_copy_message(*rpc_stream->mutable_caller_timestamp(),
                            get_request().head().rpc_stream().caller_timestamp());
    }
  }

  response_messages_.push_back(msg);
  return *msg;
}

ORBIT_SERVER_SERVICE_API const std::string &task_action_orbit_req_base::get_request_client_id() const noexcept {
  return client_id_;
}

ORBIT_SERVER_SERVICE_API void task_action_orbit_req_base::init_msg(msg_ref_type msg) {
  msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());
}

ORBIT_SERVER_SERVICE_API void task_action_orbit_req_base::init_msg(msg_ref_type msg, msg_cref_type req_msg) {
  auto *head = msg.mutable_head();
  if (nullptr == head) {
    return;
  }

  const auto &request_head = req_msg.head();
  if (request_head.has_rpc_trace()) {
    protobuf_copy_message(*head->mutable_rpc_trace(), request_head.rpc_trace());
  }

  head->set_timestamp(util::time::time_utility::get_now());
  head->set_error_code(get_response_code());
  head->set_source_task_id(0);
  head->set_destination_task_id(request_head.source_task_id());
  head->set_sequence(request_head.sequence());
}

ORBIT_SERVER_SERVICE_API std::shared_ptr<dispatcher_implement> task_action_orbit_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(orbit_msg_dispatcher::me());
}

ORBIT_SERVER_SERVICE_API const char *task_action_orbit_req_base::get_type_name() const { return "orbit"; }

ORBIT_SERVER_SERVICE_API rpc::telemetry::trace_inherit_options task_action_orbit_req_base::get_inherit_option()
    const noexcept {
  auto &req_msg = get_request();
  if (req_msg.has_head() && req_msg.head().has_rpc_request() && 0 != req_msg.head().source_task_id()) {
    return rpc::telemetry::trace_inherit_options{rpc::context::parent_mode::kParent, true, false};
  }

  return rpc::telemetry::trace_inherit_options{rpc::context::parent_mode::kLink, true, false};
}

ORBIT_SERVER_SERVICE_API rpc::telemetry::trace_start_option task_action_orbit_req_base::get_trace_option()
    const noexcept {
  rpc::telemetry::trace_start_option ret = task_action_base::get_trace_option();

  auto &req_msg = get_request();
  if (req_msg.has_head() && req_msg.head().has_rpc_trace() && !req_msg.head().rpc_trace().trace_id().empty()) {
    ret.parent_network_span = &req_msg.head().rpc_trace();
  }

  return ret;
}

ORBIT_SERVER_SERVICE_API bool task_action_orbit_req_base::is_stream_rpc() const noexcept {
  return get_request().head().has_rpc_stream();
}

ORBIT_SERVER_SERVICE_API void task_action_orbit_req_base::send_response() {
  if (response_messages_.empty()) {
    return;
  }

  if (client_id_.empty()) {
    FWLOGERROR("task {} [{}] send response to unknown client_id_", name(), get_task_id());
    response_messages_.clear();
    return;
  }

  for (std::list<message_type *>::iterator iter = response_messages_.begin(); iter != response_messages_.end();
       ++iter) {
    (*iter)->mutable_head()->set_error_code(get_response_code());
    int32_t res = orbit_msg_dispatcher::me()->send_to_client_no_wait(get_shared_context(), client_id_, **iter);
    if (res < 0) {
      FWLOGERROR("task {} [{}] send orbit response to [{}] failed, res: {}({})", name(), get_task_id(), client_id_, res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  response_messages_.clear();
}