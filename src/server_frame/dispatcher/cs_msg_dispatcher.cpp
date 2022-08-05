// Copyright 2021 atframework
// Created by owent on 2016/9/27.
//

#include "dispatcher/cs_msg_dispatcher.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include <WinSock2.h>
#endif

#include <log/log_wrapper.h>

#include <atframe/atapp.h>
#include <config/atframe_service_types.h>
#include <libatbus_protocol.h>
#include <libatgw_server_protocol.h>
#include <proto_base.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <logic/action/task_action_player_logout.h>
#include <logic/session_manager.h>

#include <utility>

#include "dispatcher/task_manager.h"

cs_msg_dispatcher::cs_msg_dispatcher() : is_closing_(false) {}
cs_msg_dispatcher::~cs_msg_dispatcher() {}

int32_t cs_msg_dispatcher::init() {
  is_closing_ = false;
  return 0;
}

int cs_msg_dispatcher::stop() {
  if (is_closing_) {
    return dispatcher_implement::stop();
  }

  session_manager::me()->remove_all(atframe::gateway::close_reason_t::EN_CRT_SERVER_CLOSED);
  is_closing_ = true;
  return dispatcher_implement::stop();
}

uint64_t cs_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
  // cs msg not allow resume task
  return 0;
}

cs_msg_dispatcher::msg_type_t cs_msg_dispatcher::pick_msg_type_id(msg_raw_t &raw_msg) { return 0; }

const std::string &cs_msg_dispatcher::pick_rpc_name(msg_raw_t &raw_msg) {
  atframework::CSMsg *real_msg = get_protobuf_msg<atframework::CSMsg>(raw_msg);
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

cs_msg_dispatcher::msg_op_type_t cs_msg_dispatcher::pick_msg_op_type(msg_raw_t &raw_msg) {
  atframework::CSMsg *real_msg = get_protobuf_msg<atframework::CSMsg>(raw_msg);
  if (nullptr == real_msg) {
    return PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP;
  }

  if (false == PROJECT_NAMESPACE_ID::EnMsgOpType_IsValid(real_msg->head().op_type())) {
    return PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP;
  }

  return static_cast<msg_op_type_t>(real_msg->head().op_type());
}

const atframework::DispatcherOptions *cs_msg_dispatcher::get_options_by_message_type(msg_type_t msg_type) {
  return nullptr;
}

void cs_msg_dispatcher::on_create_task_failed(start_data_t &start_data, int32_t error_code) {
  const std::string &rpc_name = pick_rpc_name(start_data.message);
  if (rpc_name.empty()) {
    return;
  }

  atframework::CSMsg *real_msg = get_protobuf_msg<atframework::CSMsg>(start_data.message);
  if (nullptr == real_msg) {
    return;
  }

  if (!real_msg->has_head()) {
    return;
  }

  if (!real_msg->head().has_rpc_request() || 0 == real_msg->head().session_id() ||
      0 == real_msg->head().session_bus_id()) {
    return;
  }

  session::key_t session_key;
  session_key.bus_id = real_msg->head().session_bus_id();
  session_key.session_id = real_msg->head().session_id();
  std::shared_ptr<session> sess = session_manager::me()->find(session_key);
  if (!sess) {
    FWLOGWARNING("session: [{:#x}, {}] may already be closing or already closed when receive rpc {}",
                 real_msg->head().session_bus_id(), real_msg->head().session_id(), rpc_name);
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
      trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(cs_msg_dispatcher::me());
      trace_option.parent_network_span = &real_msg->head().rpc_trace();
      child_context->setup_tracer(tracer, rpc_name, std::move(trace_option));
    }
  }

  rpc::context::message_holder<atframework::CSMsg> rsp{*child_context};
  atframework::CSMsgHead *head = rsp->mutable_head();
  if (nullptr == head) {
    FWLOGERROR("malloc header failed when pack response of {} (session: [{:#x}, {}])", rpc_name,
               real_msg->head().session_bus_id(), real_msg->head().session_id());
    return;
  }

  head->set_client_sequence(real_msg->head().client_sequence());
  if (PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND == error_code) {
    head->set_error_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  } else if (error_code < 0) {
    head->set_error_code(error_code);
  } else {
    head->set_error_code(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
  }
  head->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  head->set_session_id(real_msg->head().session_id());
  head->set_session_bus_id(real_msg->head().session_bus_id());
  head->set_timestamp(util::time::time_utility::get_now());

  head->mutable_rpc_response()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  head->mutable_rpc_response()->set_rpc_name(rpc_name);
  const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = get_registered_method(rpc_name);
  if (nullptr != method) {
    head->mutable_rpc_response()->set_type_url(method->output_type()->full_name());
  }

  int res = sess->send_msg_to_client(*rsp);
  if (res < 0) {
    FWLOGERROR("Send rpc response failed of {} (session: [{:#x}, {}]) to [{:#x}: {}] failed, res: {}({})", rpc_name,
               real_msg->head().session_bus_id(), real_msg->head().session_id(), real_msg->head().session_bus_id(),
               get_app()->convert_app_id_to_string(real_msg->head().session_bus_id()), res,
               protobuf_mini_dumper_get_error_msg(res));
  }
}

int32_t cs_msg_dispatcher::dispatch(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg) {
  if (::atframe::component::service_type::EN_ATST_GATEWAY != msg.type) {
    FWLOGERROR("message type {} invalid", msg.type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  uint64_t from_server_id = source.id;

  if (nullptr == msg.data || 0 == from_server_id) {
    FWLOGERROR("receive a message from unknown source");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  ::atframe::gw::ss_msg req_msg;
  if (false == req_msg.ParseFromArray(msg.data, static_cast<int>(msg.data_size))) {
    FWLOGERROR("receive msg of {} bytes from [{:#x}: {}] parse failed: {}", msg.data_size, from_server_id,
               get_app()->convert_app_id_to_string(from_server_id), req_msg.InitializationErrorString());
    return 0;
  }

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  switch (req_msg.body().cmd_case()) {
    case ::atframe::gw::ss_msg_body::kPost: {
      const ::atframe::gw::ss_body_post &post = req_msg.body().post();

      rpc::context ctx;
      atframework::CSMsg *cs_msg = ctx.create<atframework::CSMsg>();
      if (nullptr == cs_msg) {
        FWLOGERROR("{} create message instance failed", name());
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        break;
      }

      session::key_t session_key;
      session_key.bus_id = from_server_id;
      session_key.session_id = req_msg.head().session_id();

      std::shared_ptr<session> sess = session_manager::me()->find(session_key);
      if (!sess) {
        FWLOGERROR("session [{:#x}: {}, {}] not found, try to kickoff", session_key.bus_id,
                   get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id);
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;

        send_kickoff(session_key.bus_id, session_key.session_id, PROJECT_NAMESPACE_ID::EN_CRT_SESSION_NOT_FOUND);
        break;
      }

      dispatcher_raw_message callback_msg = dispatcher_make_default<dispatcher_raw_message>();
      ret = unpack_protobuf_msg(*cs_msg, callback_msg, reinterpret_cast<const void *>(post.content().data()),
                                post.content().size());
      if (ret != 0) {
        FWLOGERROR("{} unpack received message from [{:#x}: {}], session id: {} failed, res: %d", name(),
                   session_key.bus_id, get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id,
                   ret);
        return ret;
      }

      cs_msg->mutable_head()->set_session_bus_id(session_key.bus_id);
      cs_msg->mutable_head()->set_session_id(session_key.session_id);

      if (task_manager::me()->is_busy()) {
        cs_msg->mutable_head()->set_error_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM_BUSY);
        sess->send_msg_to_client(*cs_msg);
        FWLOGINFO("server busy and send msg back to session [{:#x}: {}, {}]", session_key.bus_id,
                  get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id);
        break;
      }

      rpc::context::tracer tracer;
      rpc::context::trace_option trace_option;
      trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
      trace_option.is_remote = true;
      trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(cs_msg_dispatcher::me());
      if (cs_msg->head().has_rpc_trace()) {
        trace_option.parent_network_span = &cs_msg->head().rpc_trace();
      } else {
        trace_option.parent_network_span = nullptr;
      }
      ctx.setup_tracer(tracer, "cs_msg_dispatcher", std::move(trace_option));

      dispatcher_result_t res = on_receive_message(ctx, callback_msg, nullptr, cs_msg->head().client_sequence());
      ret = res.result_code;
      if (ret < 0) {
        FWLOGERROR("{} on receive message callback from [{:#x}: {}, {}] failed, res: {}", name(), session_key.bus_id,
                   get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id, ret);
      }
      break;
    }
    case ::atframe::gw::ss_msg_body::kAddSession: {
      const ::atframe::gw::ss_body_session &sess_data = req_msg.body().add_session();

      session::key_t session_key;
      session_key.bus_id = from_server_id;
      session_key.session_id = req_msg.head().session_id();

      // check closing ...
      if (is_closing_) {
        FWLOGWARNING("destroy session [{:#x}: {}, {}] because app is closing", session_key.bus_id,
                     get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id);
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN;
        send_kickoff(session_key.bus_id, session_key.session_id,
                     ::atframe::gateway::close_reason_t::EN_CRT_SERVER_CLOSED);
        break;
      }

      FWLOGINFO("create new session [{:#x}: {}, {}], address: {}:{}", session_key.bus_id,
                get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id, sess_data.client_ip(),
                sess_data.client_port());

      session_manager::sess_ptr_t sess = session_manager::me()->find(session_key);
      if (sess) {
        if (sess->check_flag(session::flag_t::EN_SESSION_FLAG_CLOSING)) {
          session_manager::me()->remove(sess, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
        } else {
          FWLOGWARNING("session [{:#x}: {}, {}] already exists, address: {}:{}", session_key.bus_id,
                       get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id,
                       sess_data.client_ip(), sess_data.client_port());
          ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
          send_kickoff(session_key.bus_id, session_key.session_id,
                       ::atframe::gateway::close_reason_t::EN_CRT_SERVER_BUSY);
          break;
        }
      }
      sess = session_manager::me()->create(session_key);
      if (!sess) {
        FWLOGERROR("malloc failed");
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        send_kickoff(session_key.bus_id, session_key.session_id,
                     ::atframe::gateway::close_reason_t::EN_CRT_SERVER_BUSY);
        break;
      }

      break;
    }
    case ::atframe::gw::ss_msg_body::kRemoveSession: {
      session::key_t session_key;
      session_key.bus_id = from_server_id;
      session_key.session_id = req_msg.head().session_id();
      std::shared_ptr<session> sess = session_manager::me()->find(session_key);
      if (sess) {
        sess->set_flag(session::flag_t::EN_SESSION_FLAG_GATEWAY_REMOVED, true);
        if (sess->check_flag(session::flag_t::EN_SESSION_FLAG_CLOSING)) {
          FWLOGINFO("session [{:#x}: {}, {}] is closing, skip to create new task", session_key.bus_id,
                    get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id);
          break;
        }
      }

      // session 移除前强制update一次，用以处理debug调试断点导致task_action_player_logout被立刻认为超时
      util::time::time_utility::update();

      FWLOGINFO("remove session [{:#x}: {}, {}]", session_key.bus_id,
                get_app()->convert_app_id_to_string(session_key.bus_id), session_key.session_id);

      // logout task
      task_manager::id_t logout_task_id = 0;
      task_action_player_logout::ctor_param_t task_param;
      task_param.atgateway_session_id = session_key.session_id;
      task_param.atgateway_bus_id = session_key.bus_id;

      ret = task_manager::me()->create_task<task_action_player_logout>(logout_task_id, COPP_MACRO_STD_MOVE(task_param));
      if (0 == ret) {
        start_data_t start_data = dispatcher_make_default<dispatcher_start_data_t>();

        ret = task_manager::me()->start_task(logout_task_id, start_data);
        if (0 != ret) {
          FWLOGERROR("run logout task failed, res: {}", ret);
          session_manager::me()->remove(session_key);
        }
      } else {
        FWLOGERROR("create logout task failed, res: {}", ret);
        session_manager::me()->remove(session_key);
      }
      break;
    }
    default:
      FWLOGERROR("receive a unsupport atgateway message of invalid cmd: {}",
                 static_cast<int>(req_msg.body().cmd_case()));
      break;
  }

  return ret;
}

int32_t cs_msg_dispatcher::send_kickoff(uint64_t bus_id, uint64_t session_id, int32_t reason) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("not in a atapp");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  ::atframe::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(session_id);
  msg.mutable_head()->set_error_code(reason);

  msg.mutable_body()->mutable_kickoff_session();

  std::string packed_buffer;
  if (false == msg.SerializeToString(&packed_buffer)) {
    FWLOGERROR("try to kickoff {} with serialize failed: {}", session_id, msg.InitializationErrorString());
    return 0;
  }

  return owner->get_bus_node()->send_data(bus_id, 0, packed_buffer.data(), packed_buffer.size());
}

int32_t cs_msg_dispatcher::send_data(uint64_t bus_id, uint64_t session_id, const void *buffer, size_t len) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("not in a atapp");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (nullptr == buffer || 0 == len) {
    return 0;
  }

  ::atframe::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(session_id);

  ::atframe::gw::ss_body_post *post = msg.mutable_body()->mutable_post();

  if (nullptr == post) {
    if (0 == session_id) {
      FWLOGERROR("broadcast {} bytes data to atgateway {:#x}: {} failed when malloc post", len, bus_id,
                 get_app()->convert_app_id_to_string(bus_id));
    } else {
      FWLOGERROR("send {} bytes data to session [{:#x}: {}, {}] failed when malloc post", len, bus_id,
                 get_app()->convert_app_id_to_string(bus_id), session_id);
    }
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  post->set_content(buffer, len);

  std::string packed_buffer;
  if (false == msg.SerializeToString(&packed_buffer)) {
    FWLOGERROR("try to send {} bytes data to [{:#x}: {}] with serialize failed: {}", len, session_id,
               get_app()->convert_app_id_to_string(session_id), msg.InitializationErrorString());
    return 0;
  }

  int ret = owner->get_bus_node()->send_data(bus_id, ::atframe::component::service_type::EN_ATST_GATEWAY,
                                             packed_buffer.data(), packed_buffer.size());
  if (ret < 0) {
    if (0 == session_id) {
      FWLOGERROR("broadcast data to atgateway [{:#x}: {}] failed, res: {}", bus_id,
                 get_app()->convert_app_id_to_string(bus_id), ret);
    } else {
      FWLOGERROR("send data to session [{:#x}: {}, {}] failed, res: {}", bus_id,
                 get_app()->convert_app_id_to_string(bus_id), session_id, ret);
    }
  }

  return ret;
}

int32_t cs_msg_dispatcher::broadcast_data(uint64_t bus_id, const void *buffer, size_t len) {
  return send_data(bus_id, 0, buffer, len);
}

int32_t cs_msg_dispatcher::broadcast_data(uint64_t bus_id, const std::vector<uint64_t> &session_ids, const void *buffer,
                                          size_t len) {
  atapp::app *owner = get_app();
  if (nullptr == owner) {
    FWLOGERROR("not in a atapp");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (nullptr == buffer || 0 == len) {
    return 0;
  }

  ::atframe::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(0);

  ::atframe::gw::ss_body_post *post = msg.mutable_body()->mutable_post();

  if (nullptr == post) {
    FWLOGERROR("broadcast {} bytes data to atgateway [{:#x}: {}] failed when malloc post", len, bus_id,
               get_app()->convert_app_id_to_string(bus_id));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  post->set_content(buffer, len);

  std::string packed_buffer;
  if (false == msg.SerializeToString(&packed_buffer)) {
    FWLOGERROR("try to broadcast {} bytes data with serialize failed: {}", len, msg.InitializationErrorString());
    return 0;
  }

  int ret = owner->get_bus_node()->send_data(bus_id, ::atframe::component::service_type::EN_ATST_GATEWAY,
                                             packed_buffer.data(), packed_buffer.size());
  if (ret < 0) {
    FWLOGERROR("broadcast data to atgateway [{:#x}: {}] failed, res: {}", bus_id,
               get_app()->convert_app_id_to_string(bus_id), ret);
  }

  return ret;
}
