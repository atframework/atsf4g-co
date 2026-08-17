// Copyright 2026 atframework

#include <atframework/testing/mock_cs.h>

#include <log/log_wrapper.h>

#include <atframe/atapp.h>

#include <config/atframe_service_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>

#include <string>
#include <utility>
// clang-format on

namespace atframework {
namespace testing {

mock_cs::mock_cs() = default;
mock_cs::~mock_cs() { unbind(); }

bool mock_cs::is_active() const noexcept { return bound_; }

const cs_downstream_record *mock_cs::call_at(size_t index) const {
  if (index >= calls_.size()) {
    return nullptr;
  }
  return &calls_[index];
}

size_t mock_cs::calls(op_type op) const {
  size_t ret = 0;
  for (const auto &call : calls_) {
    if (call.op == op) {
      ++ret;
    }
  }
  return ret;
}

size_t mock_cs::calls_to(uint64_t session_id) const {
  size_t ret = 0;
  for (const auto &call : calls_) {
    if (call.session_id == session_id) {
      ++ret;
    }
  }
  return ret;
}

void mock_cs::bind() {
  if (bound_) {
    return;
  }
  cs_msg_dispatcher::set_gateway_send_hook_for_unit_test(
      [this](const cs_msg_dispatcher::unit_test_gateway_send_request &request, int32_t &result_code) {
        return on_send(request, result_code);
      });
  bound_ = true;
}

void mock_cs::unbind() {
  if (!bound_) {
    return;
  }
  cs_msg_dispatcher::set_gateway_send_hook_for_unit_test(nullptr);
  bound_ = false;
  // Uniform engine lifecycle contract: unbind resets all state so a runtime restart starts clean
  // (same as mock_ss/mock_dns).
  calls_.clear();
  send_error_ = 0;
  diagnostic_.clear();
}

bool mock_cs::on_send(const cs_msg_dispatcher::unit_test_gateway_send_request &request, int32_t &result_code) {
  cs_downstream_record record;
  record.node_id = request.node_id;
  record.session_id = request.session_id;
  if (nullptr != request.session_ids) {
    record.session_ids = *request.session_ids;
  }
  if (!record.message.ParseFromArray(reinterpret_cast<const void *>(request.data.data()),
                                     static_cast<int>(request.data.size()))) {
    FWLOGERROR("mock_cs: parse downstream server_message failed");
    diagnostic_ = "parse downstream server_message failed";
    result_code = PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
    return true;
  }

  switch (record.message.body().cmd_case()) {
    case atfw::gateway::server_message_body::kKickoffSession:
      record.op = cs_downstream_record::op_type::kickoff;
      break;
    case atfw::gateway::server_message_body::kSetRouterReq:
      record.op = cs_downstream_record::op_type::set_router;
      break;
    case atfw::gateway::server_message_body::kPost:
      record.op =
          0 == record.session_id ? cs_downstream_record::op_type::broadcast : cs_downstream_record::op_type::post;
      break;
    default:
      FWLOGERROR("mock_cs: unexpected downstream cmd_case {}", static_cast<int>(record.message.body().cmd_case()));
      diagnostic_ = "unexpected downstream cmd_case";
      result_code = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      return true;
  }

  calls_.push_back(std::move(record));
  result_code = send_error_;
  return true;
}

mock_client::mock_client(mock_cs *engine, uint64_t node_id, uint64_t session_id)
    : engine_(engine), node_id_(node_id), session_id_(session_id) {}

int32_t mock_client::dispatch_message(atfw::gateway::server_message &msg) const {
  std::string packed;
  if (!msg.SerializeToString(&packed)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  LIBATAPP_MACRO_NAMESPACE_ID::app::message_sender_t source;
  source.direct_source_id = node_id_;
  source.id = node_id_;

  LIBATAPP_MACRO_NAMESPACE_ID::app::message_t dispatch_msg;
  dispatch_msg.type = static_cast<int32_t>(::atfw::component::service_type::kAtGateway);
  dispatch_msg.data =
      gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed.data()), packed.size()};
  return cs_msg_dispatcher::me()->dispatch(source, dispatch_msg);
}

int32_t mock_client::add(gsl::string_view client_ip, uint32_t client_port) const {
  atfw::gateway::server_message msg;
  msg.mutable_head()->set_session_id(session_id_);
  atfw::gateway::server_message_body_session *add_session = msg.mutable_body()->mutable_add_session();
  add_session->set_client_ip(client_ip.data(), client_ip.size());
  add_session->set_client_port(static_cast<int32_t>(client_port));
  return dispatch_message(msg);
}

int32_t mock_client::post(const atfw::CSMsg &cs_msg) const {
  std::string content;
  if (!cs_msg.SerializeToString(&content)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  atfw::gateway::server_message msg;
  msg.mutable_head()->set_session_id(session_id_);
  msg.mutable_body()->mutable_post()->set_content(std::move(content));
  return dispatch_message(msg);
}

int32_t mock_client::remove() const {
  atfw::gateway::server_message msg;
  msg.mutable_head()->set_session_id(session_id_);
  msg.mutable_body()->mutable_remove_session();
  return dispatch_message(msg);
}

int32_t mock_client::set_router_rsp(int32_t error_code, uint64_t target_service_id,
                                    gsl::string_view target_service_name) const {
  atfw::gateway::server_message msg;
  msg.mutable_head()->set_session_id(session_id_);
  msg.mutable_head()->set_error_code(error_code);
  atfw::gateway::server_message_body_set_router *rsp = msg.mutable_body()->mutable_set_router_rsp();
  rsp->set_target_service_id(target_service_id);
  rsp->set_target_service_name(target_service_name.data(), target_service_name.size());
  return dispatch_message(msg);
}

}  // namespace testing
}  // namespace atframework
