// Copyright 2021 atframework
// Created by owent on 2016/9/27.
//

#include "dispatcher/ss_msg_dispatcher.h"

#include <log/log_wrapper.h>

#include <atframe/atapp.h>
#include <libatbus.h>
#include <libatbus_protocol.h>
#include <proto_base.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

#include <config/logic_config.h>

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

ss_msg_dispatcher::ss_msg_dispatcher() : sequence_allocator_(0) {}
ss_msg_dispatcher::~ss_msg_dispatcher() {}

int32_t ss_msg_dispatcher::init() {
  sequence_allocator_ =
      static_cast<uint64_t>(
          (util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
          << 23) +
      static_cast<uint64_t>(util::time::time_utility::get_now_usec() << 3);
  return 0;
}

uint64_t ss_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return 0;
  }

  return real_msg->head().dst_task_id();
}

ss_msg_dispatcher::msg_type_t ss_msg_dispatcher::pick_msg_type_id(msg_raw_t &raw_msg) { return 0; }

const std::string &ss_msg_dispatcher::pick_rpc_name(msg_raw_t &raw_msg) {
  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return get_empty_string();
  }

  if (!real_msg->has_head()) {
    return get_empty_string();
  }

  if (real_msg->head().has_rpc_request()) {
    return real_msg->head().rpc_request().rpc_name();
  }

  if (real_msg->head().has_rpc_stream()) {
    return real_msg->head().rpc_stream().rpc_name();
  }

  return get_empty_string();
}

ss_msg_dispatcher::msg_op_type_t ss_msg_dispatcher::pick_msg_op_type(msg_raw_t &raw_msg) {
  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP;
  }

  if (false == PROJECT_NAMESPACE_ID::EnMsgOpType_IsValid(real_msg->head().op_type())) {
    return PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP;
  }

  return static_cast<msg_op_type_t>(real_msg->head().op_type());
}

const atframework::DispatcherOptions *ss_msg_dispatcher::get_options_by_message_type(msg_type_t msg_type) {
  return nullptr;
}

int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, atframework::SSMsg &ss_msg, bool ignore_discovery) {
  if (0 == ss_msg.head().sequence()) {
    ss_msg.mutable_head()->set_sequence(allocate_sequence());
  }

  size_t msg_buf_len = ss_msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("send to proc [{:#x}: {}] failed: require {}, only have {}", bus_id,
               get_app()->convert_app_id_to_string(bus_id), msg_buf_len, tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  ss_msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes\n{}", bus_id, get_app()->convert_app_id_to_string(bus_id),
             msg_buf_len, protobuf_mini_dumper_get_readable(ss_msg));

  return send_to_proc(bus_id, buf_start, msg_buf_len, ignore_discovery);
}

int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, const void *msg_buf, size_t msg_len,
                                        EXPLICIT_UNUSED_ATTR bool ignore_discovery) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (!owner->get_bus_node()) {
    FWLOGERROR("owner app has no valid bus node");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  int res = owner->get_bus_node()->send_data(bus_id, atframe::component::message_type::EN_ATST_SS_MSG, msg_buf, msg_len,
                                             false);

  if (res < 0) {
    FWLOGERROR("send msg to proc [{:#x}: {}] {} bytes failed, res: {}", bus_id,
               get_app()->convert_app_id_to_string(bus_id), msg_len, res);
  } else {
    FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes success", bus_id, get_app()->convert_app_id_to_string(bus_id),
               msg_len);
  }

  return res;
}

bool ss_msg_dispatcher::is_target_server_available(uint64_t bus_id) const {
  if (!is_enabled()) {
    return false;
  }

  if (bus_id == get_app()->get_id()) {
    return true;
  }

  return !get_app()->get_discovery_node_by_id(bus_id);
}

bool ss_msg_dispatcher::is_target_server_available(const std::string &node_name) const {
  if (!is_enabled()) {
    return false;
  }

  if (node_name == get_app()->get_app_name()) {
    return true;
  }

  return !get_app()->get_discovery_node_by_name(node_name);
}

int32_t ss_msg_dispatcher::dispatch(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg) {
  if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.type) {
    FWLOGERROR("message type {} invalid", msg.type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (0 == source.id || (nullptr == msg.data && msg.data_size > 0)) {
    FWLOGERROR("receive a message from unknown source or without data content");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  uint64_t from_server_id = source.id;

  rpc::context ctx;
  atframework::SSMsg *ss_msg = ctx.create<atframework::SSMsg>();
  if (nullptr == ss_msg) {
    FWLOGERROR("{} create message instance failed", name());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  dispatcher_raw_message callback_msg = dispatcher_make_default<dispatcher_raw_message>();

  int32_t ret = unpack_protobuf_msg(*ss_msg, callback_msg, msg.data, msg.data_size);
  if (ret != 0) {
    FWLOGERROR("{} unpack received message from [{:#x}: {}] failed, res: {}", name(), from_server_id,
               get_app()->convert_app_id_to_string(from_server_id), ret);
    return ret;
  }
  // 不能改消息来源，可能是路由转发消息
  if (0 == ss_msg->head().bus_id()) {
    ss_msg->mutable_head()->set_bus_id(from_server_id);
  }

  rpc::context::tracer tracer;
  rpc::context::trace_option trace_option;
  trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
  trace_option.is_remote = true;
  trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  if (ss_msg->head().has_rpc_trace()) {
    trace_option.parent_network_span = &ss_msg->head().rpc_trace();
  } else {
    trace_option.parent_network_span = nullptr;
  }
  ctx.setup_tracer(tracer, "ss_msg_dispatcher", std::move(trace_option));

  dispatcher_result_t res = on_receive_message(ctx, callback_msg, nullptr, ss_msg->head().sequence());
  ret = res.result_code;
  if (ret < 0) {
    FWLOGERROR("{} dispatch message from [{:#x}: {}] failed, res: {}", name(), from_server_id,
               get_app()->convert_app_id_to_string(from_server_id), ret);
  }

  return ret;
}

int32_t ss_msg_dispatcher::on_receive_send_data_response(const atapp::app::message_sender_t &source,
                                                         const atapp::app::message_t &msg, int32_t error_code) {
  if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.type) {
    FWLOGERROR("message type {} invalid", msg.type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (0 == source.id || (nullptr == msg.data && msg.data_size > 0)) {
    FWLOGERROR("send a message from unknown source");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (error_code >= 0) {
    FWLOGDEBUG("receive_send_data_response from [{:#x}: {}]", source.id,
               get_app()->convert_app_id_to_string(source.id));
    return error_code;
  }

  if (nullptr == msg.data && msg.data_size > 0) {
    FWLOGERROR("receive_send_data_response from [{:#x}: {}] without data, res: {}", source.id,
               get_app()->convert_app_id_to_string(source.id), error_code);
    return error_code;
  }

  const void *buffer = msg.data;
  size_t len = msg.data_size;

  rpc::context ctx;
  atframework::SSMsg *ss_msg = ctx.create<atframework::SSMsg>();
  if (nullptr == ss_msg) {
    FWLOGERROR("{} create message instance failed", name());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  dispatcher_raw_message callback_msg = dispatcher_make_default<dispatcher_raw_message>();

  int32_t ret = unpack_protobuf_msg(*ss_msg, callback_msg, buffer, len);
  if (ret != 0) {
    FWLOGERROR("{} unpack on_receive_send_data_response from [{:#x}: {}] failed, res: {}", name(), source.id,
               get_app()->convert_app_id_to_string(source.id), ret);
    return ret;
  }

  ss_msg->mutable_head()->set_bus_id(source.id);
  // 转移要恢复的任务ID
  ss_msg->mutable_head()->set_dst_task_id(ss_msg->head().src_task_id());
  ss_msg->mutable_head()->set_src_task_id(0);
  ss_msg->mutable_head()->set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_SEND_FAILED);

  ret = on_send_message_failed(ctx, callback_msg, error_code, msg.message_sequence);
  if (ret < 0) {
    FWLOGERROR("{} dispatch on_send_message_failed from [{:#x}: {}] failed, res: {}", name(), source.id,
               get_app()->convert_app_id_to_string(source.id), ret);
  }

  return ret;
}

void ss_msg_dispatcher::on_create_task_failed(start_data_t &start_data, int32_t error_code) {
  const std::string &rpc_name = pick_rpc_name(start_data.message);
  if (rpc_name.empty()) {
    return;
  }

  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(start_data.message);
  if (nullptr == real_msg) {
    return;
  }

  if (!real_msg->has_head()) {
    return;
  }

  if (!real_msg->head().has_rpc_request() || 0 == real_msg->head().bus_id() || 0 == real_msg->head().src_task_id()) {
    return;
  }

  rpc::context::tracer tracer;
  std::unique_ptr<rpc::context> child_context;
  if (nullptr != start_data.context) {
    child_context.reset(new rpc::context(*start_data.context));
    if (child_context) {
      rpc::context::trace_option trace_option;
      trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
      trace_option.is_remote = true;
      trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
      trace_option.parent_network_span = &real_msg->head().rpc_trace();
      child_context->setup_tracer(tracer, rpc_name, std::move(trace_option));
    }
  }

  rpc::context::message_holder<atframework::SSMsg> rsp{*child_context};
  atframework::SSMsgHead *head = rsp->mutable_head();
  if (nullptr == head) {
    FWLOGERROR("malloc header failed when pack response of {} (source task id: {})", rpc_name,
               real_msg->head().src_task_id());
    return;
  }

  head->set_src_task_id(0);
  head->set_dst_task_id(real_msg->head().src_task_id());
  head->set_sequence(real_msg->head().sequence());
  head->set_error_code(error_code);
  head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  head->set_bus_id(real_msg->head().bus_id());
  head->set_timestamp(util::time::time_utility::get_now());

  if (real_msg->head().has_router()) {
    protobuf_copy_message(*head->mutable_router(), real_msg->head().router());
  }

  head->mutable_rpc_response()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  head->mutable_rpc_response()->set_rpc_name(rpc_name);
  const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = get_registered_method(rpc_name);
  if (nullptr != method) {
    head->mutable_rpc_response()->set_type_url(method->output_type()->full_name());
  }

  if (real_msg->head().has_router()) {
    protobuf_copy_message(*head->mutable_router(), real_msg->head().router());
  }

  head->set_player_user_id(real_msg->head().player_user_id());
  head->set_player_open_id(real_msg->head().player_open_id());
  head->set_player_zone_id(real_msg->head().player_zone_id());

  int res = send_to_proc(real_msg->head().bus_id(), *rsp);
  if (res < 0) {
    FWLOGERROR("Send create response failed of {} (source task id: {}) to [{:#x}: {}] failed, res: {}({})", rpc_name,
               real_msg->head().src_task_id(), real_msg->head().bus_id(),
               get_app()->convert_app_id_to_string(real_msg->head().bus_id()), res,
               protobuf_mini_dumper_get_error_msg(res));
  }
}

uint64_t ss_msg_dispatcher::allocate_sequence() { return ++sequence_allocator_; }
