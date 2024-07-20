// Copyright 2021 atframework
// Created by owent on 2016/9/27.
//

#include "dispatcher/ss_msg_dispatcher.h"

#include <design_pattern/singleton.h>
#include <log/log_wrapper.h>

#include <atframe/atapp.h>
#include <libatbus.h>
#include <libatbus_protocol.h>
#include <proto_base.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>

#include <opentelemetry/trace/semantic_conventions.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#include <rpc/dns/lookup.h>
#include <rpc/rpc_utils.h>
#include <rpc/telemetry/opentelemetry_utility.h>

#include <config/logic_config.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logic/logic_server_setup.h"

namespace {

struct UTIL_SYMBOL_LOCAL ss_rpc_mertrics_item {
  std::string rpc_name;

  std::chrono::microseconds min_delay;
  std::chrono::microseconds max_delay;
  std::chrono::microseconds total_delay;
  size_t total_bytes;
  size_t total_count;
};

struct UTIL_SYMBOL_LOCAL ss_rpc_mertrics_group {
  std::unordered_map<std::string, ss_rpc_mertrics_item> rpc_metrics;
};

struct UTIL_SYMBOL_LOCAL ss_rpc_mertrics_manager
    : public util::design_pattern::local_singleton<ss_rpc_mertrics_manager> {
  std::mutex lock;

  std::shared_ptr<ss_rpc_mertrics_group> current_group;
  std::shared_ptr<ss_rpc_mertrics_group> report_group;

  size_t max_collect_version = 0;
  size_t collect_version_min_delay = 0;
  size_t collect_version_max_delay = 0;
  size_t collect_version_avg_delay = 0;
  size_t collect_version_total_size = 0;
  size_t collect_version_total_count = 0;

  ss_rpc_mertrics_manager() {}
};

static void rpc_metrics_mutable_record_rpc(const std::string &rpc_name, std::chrono::microseconds delay, size_t size) {
  if (ss_rpc_mertrics_manager::is_instance_destroyed()) {
    return;
  }

  if (rpc_name.empty()) {
    return;
  }

  ss_rpc_mertrics_manager &mgr = ss_rpc_mertrics_manager::get_instance();
  std::lock_guard<std::mutex> lock_guard{mgr.lock};

  if (!mgr.current_group) {
    mgr.current_group = std::make_shared<ss_rpc_mertrics_group>();
  }
  if (!mgr.current_group) {
    return;
  }

  auto iter = mgr.current_group->rpc_metrics.find(rpc_name);
  if (iter != mgr.current_group->rpc_metrics.end()) {
    auto &record = iter->second;
    if (delay < record.min_delay) {
      record.min_delay = delay;
    }
    if (delay > record.max_delay) {
      record.max_delay = delay;
    }
    record.total_delay += delay;
    record.total_bytes += size;
    ++record.total_count;
  } else {
    auto &record = mgr.current_group->rpc_metrics[rpc_name];
    record.rpc_name = rpc_name;
    record.min_delay = delay;
    record.max_delay = delay;
    record.total_delay = delay;
    record.total_bytes = size;
    record.total_count = 1;
  }
}

static std::shared_ptr<ss_rpc_mertrics_group> rpc_metrics_get_rpc_metric_report(
    size_t(ss_rpc_mertrics_manager::*version_member)) {
  if (ss_rpc_mertrics_manager::is_instance_destroyed()) {
    return nullptr;
  }

  std::shared_ptr<ss_rpc_mertrics_group> swap_out;
  std::shared_ptr<ss_rpc_mertrics_group> ret;
  {
    ss_rpc_mertrics_manager &mgr = ss_rpc_mertrics_manager::get_instance();
    std::lock_guard<std::mutex> lock_guard{mgr.lock};

    ++(mgr.*version_member);
    if (mgr.current_group && (mgr.*version_member) > mgr.max_collect_version) {
      mgr.max_collect_version = (mgr.*version_member);
      mgr.report_group.swap(mgr.current_group);
      mgr.current_group.swap(swap_out);
    }

    ret = mgr.report_group;
  }

  return ret;
}

}  // namespace

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

SERVER_FRAME_API void ss_msg_dispatcher::ready() {
  // setup metrics
  setup_metrics();
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

  return real_msg->head().destination_task_id();
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

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(uint64_t node_id, atframework::SSMsg &ss_msg,
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
    FWLOGERROR("send to proc [{:#x}: {}] failed: require {}, only have {}", node_id,
               get_app()->convert_app_id_to_string(node_id), msg_buf_len, tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  ss_msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes\n{}", node_id, get_app()->convert_app_id_to_string(node_id),
             msg_buf_len, protobuf_mini_dumper_get_readable(ss_msg));

  return send_to_proc(node_id, buf_start, msg_buf_len, ss_msg.head().sequence(), ignore_discovery);
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(uint64_t node_id, const void *msg_buf, size_t msg_len,
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

  int res = convert_from_atapp_error_code(
      owner->send_message(node_id, atframe::component::message_type::EN_ATST_SS_MSG, msg_buf, msg_len, &sequence));
  if (res < 0) {
    FWLOGERROR("send msg to proc [{:#x}: {}] {} bytes failed, res: {}", node_id,
               get_app()->convert_app_id_to_string(node_id), msg_len, res);
  } else {
    FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes success", node_id, get_app()->convert_app_id_to_string(node_id),
               msg_len);
  }

  return res;
}

SERVER_FRAME_API int32_t ss_msg_dispatcher::send_to_proc(const std::string &node_name, atframework::SSMsg &ss_msg,
                                                         bool ignore_discovery) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("module not attached to a atapp");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  atapp::etcd_discovery_node::ptr_t node_ptr = owner->get_discovery_node_by_name(node_name);
  if (!node_ptr) {
    FWLOGERROR("send msg to proc {} failed: not found", node_name);
    return PROJECT_NAMESPACE_ID::err::EN_ATBUS_ERR_ATNODE_NOT_FOUND;
  }

  return send_to_proc(*node_ptr, ss_msg, ignore_discovery);
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

  int res = convert_from_atapp_error_code(owner->send_message(
      node.get_discovery_info().name(), atframe::component::message_type::EN_ATST_SS_MSG, msg_buf, msg_len, &sequence));
  if (res < 0) {
    FWLOGERROR("{} send msg to proc {} {} bytes failed, res: {}", name(), node.get_discovery_info().name(), msg_len,
               res);
  } else {
    FWLOGDEBUG("{} send msg to proc {} {} bytes success", name(), node.get_discovery_info().name(), msg_len);
  }

  return res;
}

SERVER_FRAME_API bool ss_msg_dispatcher::is_target_server_available(uint64_t node_id) const {
  if (nullptr == get_app()) {
    return false;
  }

  if (!is_enabled()) {
    return false;
  }

  if (node_id == get_app()->get_id()) {
    return true;
  }

  return !get_app()->get_discovery_node_by_id(node_id);
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
  if (0 == ss_msg->head().node_id()) {
    ss_msg->mutable_head()->set_node_id(from_server_id);
  }

  // Metrics RPC from response
  do {
    if (!ss_msg->head().has_rpc_response()) {
      break;
    }

    auto &rpc_response = ss_msg->head().rpc_response();
    if (!rpc_response.has_caller_timestamp()) {
      break;
    }

    if (rpc_response.caller_timestamp().seconds() <= 0) {
      break;
    }

    util::time::time_utility::update();
    auto delay = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::seconds{util::time::time_utility::get_sys_now() - rpc_response.caller_timestamp().seconds()} +
        std::chrono::nanoseconds{util::time::time_utility::get_now_nanos() - rpc_response.caller_timestamp().nanos()});

    // Unexcepted duration is ignored
    if (delay < std::chrono::microseconds::zero()) {
      break;
    }

    rpc_metrics_mutable_record_rpc(rpc_response.rpc_name(), delay, ss_msg->body_bin().size());
  } while (false);

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
  rpc::telemetry::trace_attribute_pair_type internal_rpc_trace_attributes[] = {
      {opentelemetry::trace::SemanticConventions::kRpcSystem, "internal"},
      {opentelemetry::trace::SemanticConventions::kRpcService, "ss_msg_dispatcher"},
      {opentelemetry::trace::SemanticConventions::kRpcMethod, "ss_msg_dispatcher"}};
  trace_start_option.attributes = internal_rpc_trace_attributes;
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

  ss_msg->mutable_head()->set_node_id(source.id);
  // 转移要恢复的任务ID
  ss_msg->mutable_head()->set_destination_task_id(ss_msg->head().source_task_id());
  ss_msg->mutable_head()->set_source_task_id(0);
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

  if (!real_msg->head().has_rpc_request() || 0 == real_msg->head().node_id() ||
      0 == real_msg->head().source_task_id()) {
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
               real_msg->head().source_task_id());
    return;
  }

  head->set_source_task_id(0);
  head->set_destination_task_id(real_msg->head().source_task_id());
  head->set_sequence(real_msg->head().sequence());
  head->set_error_code(error_code);
  head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  head->set_node_id(real_msg->head().node_id());
  head->set_timestamp(util::time::time_utility::get_now());

  do {
    auto rpc_response = head->mutable_rpc_response();
    if (nullptr == rpc_response) {
      break;
    }
    rpc_response->set_version(logic_config::me()->get_atframework_settings().rpc_version());
    rpc_response->set_rpc_name(rpc_name);
    const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = get_registered_method(rpc_name);
    if (nullptr != method) {
      rpc_response->set_type_url(method->output_type()->full_name());
    }
    rpc_response->set_caller_node_id(real_msg->head().node_id());
    if (real_msg->head().has_rpc_forward() && real_msg->head().rpc_forward().transparent()) {
      uint64_t node_id = real_msg->head().rpc_forward().forward_for_node_id();
      if (node_id != 0) {
        rpc_response->set_caller_node_id(node_id);
      }
    }
    if (real_msg->head().has_rpc_forward() && real_msg->head().rpc_forward().transparent()) {
      if (!real_msg->head().rpc_forward().forward_for_node_name().empty()) {
        rpc_response->set_caller_node_name(real_msg->head().rpc_forward().forward_for_node_name());
      }
    }
    if (rpc_response->caller_node_name().empty()) {
      rpc_response->set_caller_node_name(real_msg->head().node_name());
    }

    protobuf_copy_message(*rpc_response->mutable_caller_timestamp(), real_msg->head().rpc_request().caller_timestamp());
  } while (false);

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

  int res = send_to_proc(real_msg->head().node_id(), *rsp);
  if (res < 0) {
    FWLOGERROR("Send create response failed of {} (source task id: {}) to [{:#x}: {}] failed, res: {}({})", rpc_name,
               real_msg->head().source_task_id(), real_msg->head().node_id(),
               get_app()->convert_app_id_to_string(real_msg->head().node_id()), res,
               protobuf_mini_dumper_get_error_msg(res));
  }
}

SERVER_FRAME_API uint64_t ss_msg_dispatcher::allocate_sequence() { return ++sequence_allocator_; }

void ss_msg_dispatcher::setup_metrics() {
  rpc::telemetry::global_service::add_on_ready([]() {
    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_inserver_rpc",
        {"atframework_inserver_rpc_delay_min", "", "us"}, [](opentelemetry::metrics::ObserverResult &result) {
          auto report = rpc_metrics_get_rpc_metric_report(&ss_rpc_mertrics_manager::collect_version_min_delay);
          if (!report) {
            return;
          }

          for (auto &method : report->rpc_metrics) {
            auto service_end = method.second.rpc_name.find_last_of('.');
            opentelemetry::nostd::string_view service_name = "UNKNOWN";
            if (service_end != std::string::npos) {
              service_name = opentelemetry::nostd::string_view{method.second.rpc_name.c_str(), service_end};
            }
            rpc::telemetry::trace_attribute_pair_type internal_attributes[] = {
                {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
                {opentelemetry::trace::SemanticConventions::kRpcService, service_name},
                {opentelemetry::trace::SemanticConventions::kRpcMethod, method.second.rpc_name}};

            rpc::telemetry::opentelemetry_utility::global_metics_observe_record_extend_attrubutes(
                result, static_cast<int64_t>(method.second.min_delay.count()), internal_attributes);
          }
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_inserver_rpc",
        {"atframework_inserver_rpc_delay_max", "", "us"}, [](opentelemetry::metrics::ObserverResult &result) {
          auto report = rpc_metrics_get_rpc_metric_report(&ss_rpc_mertrics_manager::collect_version_min_delay);
          if (!report) {
            return;
          }

          for (auto &method : report->rpc_metrics) {
            auto service_end = method.second.rpc_name.find_last_of('.');
            opentelemetry::nostd::string_view service_name = "UNKNOWN";
            if (service_end != std::string::npos) {
              service_name = opentelemetry::nostd::string_view{method.second.rpc_name.c_str(), service_end};
            }
            rpc::telemetry::trace_attribute_pair_type internal_attributes[] = {
                {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
                {opentelemetry::trace::SemanticConventions::kRpcService, service_name},
                {opentelemetry::trace::SemanticConventions::kRpcMethod, method.second.rpc_name}};

            rpc::telemetry::opentelemetry_utility::global_metics_observe_record_extend_attrubutes(
                result, static_cast<int64_t>(method.second.max_delay.count()), internal_attributes);
          }
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_inserver_rpc",
        {"atframework_inserver_rpc_delay_avg", "", "us"}, [](opentelemetry::metrics::ObserverResult &result) {
          auto report = rpc_metrics_get_rpc_metric_report(&ss_rpc_mertrics_manager::collect_version_min_delay);
          if (!report) {
            return;
          }

          for (auto &method : report->rpc_metrics) {
            if (method.second.total_count <= 0) {
              continue;
            }
            auto service_end = method.second.rpc_name.find_last_of('.');
            opentelemetry::nostd::string_view service_name = "UNKNOWN";
            if (service_end != std::string::npos) {
              service_name = opentelemetry::nostd::string_view{method.second.rpc_name.c_str(), service_end};
            }
            rpc::telemetry::trace_attribute_pair_type internal_attributes[] = {
                {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
                {opentelemetry::trace::SemanticConventions::kRpcService, service_name},
                {opentelemetry::trace::SemanticConventions::kRpcMethod, method.second.rpc_name}};

            rpc::telemetry::opentelemetry_utility::global_metics_observe_record_extend_attrubutes(
                result,
                static_cast<int64_t>(method.second.total_delay.count()) /
                    static_cast<int64_t>(method.second.total_count),
                internal_attributes);
          }
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_inserver_rpc",
        {"atframework_inserver_rpc_count", "", ""}, [](opentelemetry::metrics::ObserverResult &result) {
          auto report = rpc_metrics_get_rpc_metric_report(&ss_rpc_mertrics_manager::collect_version_min_delay);
          if (!report) {
            return;
          }

          for (auto &method : report->rpc_metrics) {
            auto service_end = method.second.rpc_name.find_last_of('.');
            opentelemetry::nostd::string_view service_name = "UNKNOWN";
            if (service_end != std::string::npos) {
              service_name = opentelemetry::nostd::string_view{method.second.rpc_name.c_str(), service_end};
            }
            rpc::telemetry::trace_attribute_pair_type internal_attributes[] = {
                {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
                {opentelemetry::trace::SemanticConventions::kRpcService, service_name},
                {opentelemetry::trace::SemanticConventions::kRpcMethod, method.second.rpc_name}};

            rpc::telemetry::opentelemetry_utility::global_metics_observe_record_extend_attrubutes(
                result, static_cast<int64_t>(method.second.total_count), internal_attributes);
          }
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "atframework_inserver_rpc",
        {"atframework_inserver_rpc_size", "", ""}, [](opentelemetry::metrics::ObserverResult &result) {
          auto report = rpc_metrics_get_rpc_metric_report(&ss_rpc_mertrics_manager::collect_version_min_delay);
          if (!report) {
            return;
          }

          for (auto &method : report->rpc_metrics) {
            auto service_end = method.second.rpc_name.find_last_of('.');
            opentelemetry::nostd::string_view service_name = "UNKNOWN";
            if (service_end != std::string::npos) {
              service_name = opentelemetry::nostd::string_view{method.second.rpc_name.c_str(), service_end};
            }
            rpc::telemetry::trace_attribute_pair_type internal_attributes[] = {
                {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
                {opentelemetry::trace::SemanticConventions::kRpcService, service_name},
                {opentelemetry::trace::SemanticConventions::kRpcMethod, method.second.rpc_name}};

            rpc::telemetry::opentelemetry_utility::global_metics_observe_record_extend_attrubutes(
                result, static_cast<int64_t>(method.second.total_bytes), internal_attributes);
          }
        });
  });
}

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
