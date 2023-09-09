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

#include <rpc/dns/lookup.h>
#include <rpc/rpc_utils.h>

#include <config/logic_config.h>

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logic/logic_server_setup.h"

#if defined(DS_BATTLE_SDK_DLL) && DS_BATTLE_SDK_DLL
#  if defined(DS_BATTLE_SDK_NATIVE) && DS_BATTLE_SDK_NATIVE
UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(ss_msg_dispatcher);
#  else
UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(ss_msg_dispatcher);
#  endif
#else
UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(ss_msg_dispatcher);
#endif

SERVER_FRAME_API ss_msg_dispatcher::ss_msg_dispatcher() : sequence_allocator_(0) {}

SERVER_FRAME_API ss_msg_dispatcher::~ss_msg_dispatcher() {}

SERVER_FRAME_API int32_t ss_msg_dispatcher::init() {
  sequence_allocator_ =
      static_cast<uint64_t>(
          (util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
          << 23) +
      static_cast<uint64_t>(util::time::time_utility::get_now_usec() << 3);
  return 0;
}

SERVER_FRAME_API const char *ss_msg_dispatcher::name() const { return "ss_msg_dispatcher"; }

SERVER_FRAME_API int ss_msg_dispatcher::stop() {
  int ret = dispatcher_implement::stop();
  if (!running_dns_lookup_.empty()) {
    ret = 1;

    for (auto &dns_request : running_dns_lookup_) {
      if (dns_request.second) {
        uv_cancel(reinterpret_cast<uv_req_t *>(&dns_request.second->request));
      }
    }
  }

  return ret;
}

SERVER_FRAME_API int ss_msg_dispatcher::tick() {
  int ret = dispatcher_implement::tick();
  time_t sys_now = 0;

  while (!running_dns_lookup_.empty()) {
    if (sys_now == 0) {
      sys_now = util::time::time_utility::get_sys_now();
    }

    if (!running_dns_lookup_.front().second) {
      running_dns_lookup_.pop_front();
      continue;
    }

    if (sys_now <= running_dns_lookup_.front().second->timeout_timepoint) {
      break;
    }

    uv_cancel(reinterpret_cast<uv_req_t *>(&running_dns_lookup_.front().second->request));
    running_dns_lookup_.pop_front();
    if (ret >= 0) {
      ++ret;
    }
  }

  return ret;
}

SERVER_FRAME_API uint64_t ss_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return 0;
  }

  return real_msg->head().dst_task_id();
}

SERVER_FRAME_API ss_msg_dispatcher::msg_type_t ss_msg_dispatcher::pick_msg_type_id(msg_raw_t &) { return 0; }

SERVER_FRAME_API const std::string &ss_msg_dispatcher::pick_rpc_name(msg_raw_t &raw_msg) {
  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return get_empty_string();
  }

  return pick_rpc_name(*real_msg);
}

SERVER_FRAME_API const std::string &ss_msg_dispatcher::pick_rpc_name(const atframework::SSMsg &ss_msg) {
  if (!ss_msg.has_head()) {
    return get_empty_string();
  }

  if (ss_msg.head().has_rpc_request()) {
    return ss_msg.head().rpc_request().rpc_name();
  }

  if (ss_msg.head().has_rpc_stream()) {
    return ss_msg.head().rpc_stream().rpc_name();
  }

  return get_empty_string();
}

SERVER_FRAME_API ss_msg_dispatcher::msg_op_type_t ss_msg_dispatcher::pick_msg_op_type(msg_raw_t &raw_msg) {
  atframework::SSMsg *real_msg = get_protobuf_msg<atframework::SSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP;
  }

  if (false == PROJECT_NAMESPACE_ID::EnMsgOpType_IsValid(real_msg->head().op_type())) {
    return PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP;
  }

  return static_cast<msg_op_type_t>(real_msg->head().op_type());
}

SERVER_FRAME_API const atframework::DispatcherOptions *ss_msg_dispatcher::get_options_by_message_type(msg_type_t) {
  return nullptr;
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, atframework::SSMsg &ss_msg,
                                                         bool ignore_discovery) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp, maybe not initialized or already closed");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

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

  return send_to_proc(bus_id, buf_start, msg_buf_len, ss_msg.head().sequence(), ignore_discovery);
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, const void *msg_buf, size_t msg_len,
                                                         uint64_t sequence,
                                                         EXPLICIT_UNUSED_ATTR bool ignore_discovery) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp, maybe not initialized or already closed");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (!owner->get_bus_node()) {
    FWLOGERROR("owner app has no valid bus node");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (0 == sequence) {
    sequence = owner->get_bus_node()->alloc_msg_seq();
  }

  int res = owner->send_message(bus_id, atframe::component::message_type::EN_ATST_SS_MSG, msg_buf, msg_len, &sequence);

  if (res < 0) {
    FWLOGERROR("send msg to proc [{:#x}: {}] {} bytes failed, res: {}", bus_id,
               get_app()->convert_app_id_to_string(bus_id), msg_len, res);
  } else {
    FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes success", bus_id, get_app()->convert_app_id_to_string(bus_id),
               msg_len);
  }

  return res;
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(const atapp::etcd_discovery_node &node,
                                                         atframework::SSMsg &ss_msg, bool ignore_discovery) {
  if (node.get_discovery_info().id() != 0) {
    return send_to_proc(node.get_discovery_info().id(), ss_msg, ignore_discovery);
  }

  if (0 == ss_msg.head().sequence()) {
    ss_msg.mutable_head()->set_sequence(allocate_sequence());
  }

  size_t msg_buf_len = ss_msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("send to proc {} failed: require {}, only have {}", node.get_discovery_info().name(), msg_buf_len,
               tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  ss_msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("send msg to proc {} {} bytes\n{}", node.get_discovery_info().name(), msg_buf_len,
             protobuf_mini_dumper_get_readable(ss_msg));

  return send_to_proc(node, buf_start, msg_buf_len, ss_msg.head().sequence(), ignore_discovery);
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(const atapp::etcd_discovery_node &node, const void *msg_buf,
                                                         size_t msg_len, uint64_t sequence, bool ignore_discovery) {
  if (node.get_discovery_info().id() != 0) {
    return send_to_proc(node.get_discovery_info().id(), msg_buf, msg_len, sequence, ignore_discovery);
  }

  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  int res = owner->send_message(node.get_discovery_info().name(), atframe::component::message_type::EN_ATST_SS_MSG,
                                msg_buf, msg_len, &sequence);
  if (res < 0) {
    FWLOGERROR("{} send msg to proc {} {} bytes failed, res: {}", name(), node.get_discovery_info().name(), msg_len,
               res);
  } else {
    FWLOGDEBUG("{} send msg to proc {} {} bytes success", name(), node.get_discovery_info().name(), msg_len);
  }

  return res;
}

SERVER_FRAME_API bool ss_msg_dispatcher::is_target_server_available(uint64_t bus_id) const {
  if (nullptr == get_app()) {
    return false;
  }

  if (!is_enabled()) {
    return false;
  }

  if (bus_id == get_app()->get_id()) {
    return true;
  }

  return !get_app()->get_discovery_node_by_id(bus_id);
}

SERVER_FRAME_API bool ss_msg_dispatcher::is_target_server_available(const std::string &node_name) const {
  if (nullptr == get_app()) {
    return false;
  }

  if (!is_enabled()) {
    return false;
  }

  if (node_name == get_app()->get_app_name()) {
    return true;
  }

  return !get_app()->get_discovery_node_by_name(node_name);
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::broadcast(atframework::SSMsg &ss_msg, const ss_msg_logic_index &index,
                                                      ::atapp::protocol::atapp_metadata *metadata) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp, maybe not initialized or already closed");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  const atapp::etcd_discovery_set *discovery_set = nullptr;
  atapp::etcd_discovery_set::ptr_t discovery_set_ptr_lifetime;
  if (index.type_id == 0 && index.zone_id == 0 && index.type_name.empty()) {
    discovery_set = &owner->get_global_discovery();
  } else {
    auto common_mod = logic_server_last_common_module();
    if (nullptr == common_mod) {
      FWLOGERROR("common module is required to broadcast");
      return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
    }

    if (index.type_id != 0 && index.zone_id != 0) {
      discovery_set_ptr_lifetime = common_mod->get_discovery_index_by_type_zone(index.type_id, index.zone_id);
    } else if (!index.type_name.empty() && index.zone_id != 0) {
      discovery_set_ptr_lifetime =
          common_mod->get_discovery_index_by_type_zone(static_cast<std::string>(index.type_name), index.zone_id);
    } else if (index.type_id != 0) {
      discovery_set_ptr_lifetime = common_mod->get_discovery_index_by_type(index.type_id);
    } else if (index.zone_id != 0) {
      discovery_set_ptr_lifetime = common_mod->get_discovery_index_by_zone(index.zone_id);
    } else if (!index.type_name.empty()) {
      discovery_set_ptr_lifetime = common_mod->get_discovery_index_by_type(static_cast<std::string>(index.type_name));
    }
    discovery_set = discovery_set_ptr_lifetime.get();
  }

  if (discovery_set == nullptr) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (discovery_set->empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  auto &server_nodes = discovery_set->get_sorted_nodes(metadata);
  if (server_nodes.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (0 == ss_msg.head().sequence()) {
    ss_msg.mutable_head()->set_sequence(allocate_sequence());
  }

  size_t msg_buf_len = ss_msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("broadcast message {} failed: require {}, only have {}", pick_rpc_name(ss_msg), msg_buf_len,
               tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  ss_msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("broadcast message {} to {} nodes with {} bytes\n{}", pick_rpc_name(ss_msg), server_nodes.size(),
             msg_buf_len, protobuf_mini_dumper_get_readable(ss_msg));

  int ret = 0;
  for (auto &server_node : server_nodes) {
    if (!server_node) {
      continue;
    }

    int32_t res = send_to_proc(*server_node, buf_start, msg_buf_len, ss_msg.head().sequence(), false);
    if (res < 0) {
      if (ret >= 0) {
        ret = res;
      }
      FWLOGERROR("broadcast message {} and send {} bytes to {}[{}] failed, error code: {}({})", pick_rpc_name(ss_msg),
                 msg_buf_len, server_node->get_discovery_info().name(), server_node->get_discovery_info().id(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  return ret;
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::dispatch(const atapp::app::message_sender_t &source,
                                                     const atapp::app::message_t &msg) {
  if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.type) {
    FWLOGERROR("message type {} invalid", msg.type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (0 == source.id || (nullptr == msg.data && msg.data_size > 0)) {
    FWLOGERROR("receive a message from unknown source or without data content");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  uint64_t from_server_id = source.id;

  rpc::context ctx{rpc::context::create_without_task()};
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
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
  trace_start_option.is_remote = true;
  trace_start_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  if (ss_msg->head().has_rpc_trace()) {
    trace_start_option.parent_network_span = &ss_msg->head().rpc_trace();
  } else {
    trace_start_option.parent_network_span = nullptr;
  }
  ctx.setup_tracer(tracer, "ss_msg_dispatcher", std::move(trace_start_option));

  dispatcher_result_t res = on_receive_message(ctx, callback_msg, nullptr, ss_msg->head().sequence());
  ret = res.result_code;
  if (ret < 0) {
    FWLOGERROR("{} dispatch message from [{:#x}: {}] failed, res: {}", name(), from_server_id,
               get_app()->convert_app_id_to_string(from_server_id), ret);
  }

  return tracer.finish({ret, {}});
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::on_receive_send_data_response(const atapp::app::message_sender_t &source,
                                                                          const atapp::app::message_t &msg,
                                                                          int32_t error_code) {
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

  rpc::context ctx{rpc::context::create_without_task()};
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

SERVER_FRAME_API void ss_msg_dispatcher::on_create_task_failed(dispatcher_start_data_type &start_data,
                                                               int32_t error_code) {
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
      rpc::context::trace_start_option trace_start_option;
      trace_start_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
      trace_start_option.is_remote = true;
      trace_start_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
      trace_start_option.parent_network_span = &real_msg->head().rpc_trace();
      child_context->setup_tracer(tracer, rpc_name, std::move(trace_start_option));
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

SERVER_FRAME_API uint64_t ss_msg_dispatcher::allocate_sequence() { return ++sequence_allocator_; }

SERVER_FRAME_API void ss_msg_dispatcher::dns_lookup_callback(uv_getaddrinfo_t *req, int /*status*/,
                                                             struct addrinfo *result) noexcept {
  std::shared_ptr<dns_lookup_async_data> *lifetime_ptr =
      reinterpret_cast<std::shared_ptr<dns_lookup_async_data> *>(req->data);

  do {
    if (!ss_msg_dispatcher::is_instance_destroyed()) {
      ss_msg_dispatcher::me()->running_dns_lookup_.erase((*lifetime_ptr)->rpc_sequence);
    }

    size_t count = 0;
    struct addrinfo *begin = result;
    while (nullptr != begin) {
      ++count;
      begin = begin->ai_next;
    }

    rpc::dns::details::callback_data_type records;
    records.reserve(count);
    for (begin = result; nullptr != begin; begin = begin->ai_next) {
      rpc::dns::address_record record;
      if (AF_INET == begin->ai_family) {
        sockaddr_in *res_c = reinterpret_cast<sockaddr_in *>(begin->ai_addr);
        char ip[18] = {0};
        if (0 != uv_ip4_name(res_c, ip, sizeof(ip) - 1)) {
          continue;
        }

        record.type = rpc::dns::address_type::kA;
        record.address = ip;
      } else if (AF_INET6 == begin->ai_family) {
        sockaddr_in6 *res_c = reinterpret_cast<sockaddr_in6 *>(begin->ai_addr);
        char ip[48] = {0};
        if (0 != uv_ip6_name(res_c, ip, sizeof(ip) - 1)) {
          continue;
        }

        record.type = rpc::dns::address_type::kAAAA;
        record.address = ip;
      } else {
        continue;
      }

      records.emplace_back(std::move(record));
    }

    if (!task_manager::is_instance_destroyed()) {
      auto task_inst = task_manager::me()->get_task((*lifetime_ptr)->task_id);
      if (!task_type_trait::empty(task_inst)) {
        dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
        callback_data.message.message_type = reinterpret_cast<uintptr_t>((*lifetime_ptr)->rpc_type_address);
        callback_data.message.msg_addr = reinterpret_cast<void *>(&records);
        callback_data.sequence = (*lifetime_ptr)->rpc_sequence;
        rpc::custom_resume(task_inst, callback_data);
      }
    }
  } while (false);

  if (nullptr != result) {
    uv_freeaddrinfo(result);
  }

  if (nullptr != lifetime_ptr) {
    delete lifetime_ptr;
  }
}

SERVER_FRAME_API void *ss_msg_dispatcher::get_dns_lookup_rpc_type() noexcept {
  return reinterpret_cast<void *>(&running_dns_lookup_);
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_dns_lookup(gsl::string_view domain, uint64_t sequence,
                                                            uint64_t task_id) {
  if (domain.empty() || 0 == task_id) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (0 == sequence) {
    sequence = allocate_sequence();
  }

  std::shared_ptr<dns_lookup_async_data> async_data = std::make_shared<dns_lookup_async_data>();
  if (!async_data) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  std::shared_ptr<dns_lookup_async_data> *lifetime_ptr = new std::shared_ptr<dns_lookup_async_data>(async_data);
  if (nullptr == lifetime_ptr) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  async_data->start_timepoint = util::time::time_utility::get_sys_now();
  time_t timeout_conf = logic_config::me()->get_logic().dns().lookup_timeout().seconds();
  if (timeout_conf <= 0) {
    timeout_conf = 5;
  }
  async_data->timeout_timepoint = async_data->start_timepoint + timeout_conf;
  async_data->domain = static_cast<std::string>(domain);
  async_data->task_id = task_id;
  async_data->rpc_type_address = get_dns_lookup_rpc_type();
  async_data->rpc_sequence = sequence;
  async_data->request.data = reinterpret_cast<void *>(lifetime_ptr);

  uv_loop_t *loop = nullptr;
  if (nullptr != get_app() && get_app()->get_bus_node()) {
    loop = get_app()->get_bus_node()->get_evloop();
  }
  if (nullptr == loop) {
    loop = uv_default_loop();
  }

  int uv_res =
      uv_getaddrinfo(loop, &async_data->request, dns_lookup_callback, async_data->domain.c_str(), nullptr, nullptr);
  if (0 != uv_res) {
    FWLOGERROR("Try to get addrinfo of {} failed, libuv res: {}({})", async_data->domain, uv_res, uv_err_name(uv_res));
    delete lifetime_ptr;
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  }

  running_dns_lookup_.insert_key_value(sequence, std::move(async_data));
  return 0;
}
