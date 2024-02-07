// Copyright 2021 atframework

#include "data/session.h"

#include <algorithm/hash.h>
#include <log/log_sink_file_backend.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/cs_msg_dispatcher.h>
#include <proto_base.h>

#include <utility/protobuf_mini_dumper.h>

#include <config/logic_config.h>

#include <opentelemetry/trace/semantic_conventions.h>
#include <rpc/rpc_context.h>
#include <rpc/telemetry/semantic_conventions.h>

#include <utility>

#include "data/player_cache.h"

session::key_t::key_t() : node_id(0), session_id(0) {}
session::key_t::key_t(const std::pair<uint64_t, uint64_t> &p) : node_id(p.first), session_id(p.second) {}

bool session::key_t::operator==(const key_t &r) const { return node_id == r.node_id && session_id == r.session_id; }

bool session::key_t::operator!=(const key_t &r) const { return !((*this) == r); }

bool session::key_t::operator<(const key_t &r) const {
  if (node_id != r.node_id) {
    return node_id < r.node_id;
  }
  return session_id < r.session_id;
}

bool session::key_t::operator<=(const key_t &r) const { return (*this) < r || (*this) == r; }

bool session::key_t::operator>(const key_t &r) const {
  if (node_id != r.node_id) {
    return node_id > r.node_id;
  }
  return session_id > r.session_id;
}

bool session::key_t::operator>=(const key_t &r) const { return (*this) > r || (*this) == r; }

session::flag_guard_t::flag_guard_t() : flag_(flag_t::EN_SESSION_FLAG_NONE), owner_(nullptr) {}
session::flag_guard_t::~flag_guard_t() { reset(); }

void session::flag_guard_t::setup(session &owner, flag_t::type f) {
  if (flag_t::EN_SESSION_FLAG_NONE == f) {
    return;
  }

  // 一次只能设置一个flag
  if (f & (f - 1)) {
    return;
  }

  // 已被其他地方设置
  if (owner.check_flag(f)) {
    return;
  }

  reset();
  owner_ = &owner;
  flag_ = f;
  owner_->set_flag(flag_, true);
}

void session::flag_guard_t::reset() {
  if (owner_ && flag_t::EN_SESSION_FLAG_NONE != flag_) {
    owner_->set_flag(flag_, false);
  }

  owner_ = nullptr;
  flag_ = flag_t::EN_SESSION_FLAG_NONE;
}

session::session() : flags_(0), login_task_id_(0), session_sequence_(0), cached_zone_id_(0), cached_user_id_(0) {
  id_.node_id = 0;
  id_.session_id = 0;
}

session::~session() {
  FWLOGDEBUG("session [{:#x}, {}] destroyed", id_.node_id, id_.session_id);

  if (actor_log_writter_) {
    util::log::log_wrapper::caller_info_t caller = util::log::log_wrapper::caller_info_t(
        util::log::log_formatter::level_t::LOG_LW_INFO, {}, __FILE__, __LINE__, __FUNCTION__);
    actor_log_writter_->format_log(caller, "------------ session: {:#x}:{} destroyed ------------", get_key().node_id,
                                   get_key().session_id);
  }

  if (actor_log_otel_) {
    std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue> attributes[] = {
        {"gateway.node_id", get_key().node_id},
        {"user.id", cached_user_id_},
        {"user.zone_id", cached_zone_id_},
        {"session.event", "destroy"},
        {opentelemetry::trace::SemanticConventions::kSessionId, get_key().session_id}};
    actor_log_otel_->Info(util::log::format("------------ session: {:#x}:{} destroyed ------------", get_key().node_id,
                                            get_key().session_id),
                          opentelemetry::common::MakeAttributes(attributes));
  }
}

bool session::is_closing() const noexcept { return check_flag(flag_t::EN_SESSION_FLAG_CLOSING); }

bool session::is_closed() const noexcept { return check_flag(flag_t::EN_SESSION_FLAG_CLOSED); }

bool session::is_valid() const noexcept {
  return 0 == (flags_ & (flag_t::EN_SESSION_FLAG_CLOSING | flag_t::EN_SESSION_FLAG_CLOSED |
                         flag_t::EN_SESSION_FLAG_GATEWAY_REMOVED));
}

void session::set_player(std::shared_ptr<player_cache> u) {
  player_ = u;

  if (u) {
    cached_zone_id_ = u->get_zone_id();
    cached_user_id_ = u->get_user_id();
  }

  if (u && logic_config::me()->get_logic().session().enable_actor_log()) {
    create_actor_log_writter();
  }
}

std::shared_ptr<player_cache> session::get_player() const { return player_.lock(); }

int32_t session::send_msg_to_client(rpc::context &ctx, atframework::CSMsg &msg) {
  if (0 == msg.head().server_sequence()) {
    std::shared_ptr<player_cache> user = get_player();
    if (user) {
      return send_msg_to_client(ctx, msg, user->alloc_server_sequence());
    }
  }
  return send_msg_to_client(ctx, msg, msg.head().server_sequence());
}

int32_t session::send_msg_to_client(rpc::context &ctx, atframework::CSMsg &msg, uint64_t server_sequence) {
  if (!msg.has_head() || msg.head().timestamp() == 0) {
    msg.mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  msg.mutable_head()->set_server_sequence(server_sequence);
  alloc_session_sequence(msg);

  size_t msg_buf_len = msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("send to gateway [{:#x}, {}] failed: require {}, only have {}", id_.node_id, id_.session_id, msg_buf_len,
               tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG(
      "send msg to client:[{:#x}, {}] {} bytes.(session sequence: {}, client sequence: {}, server sequence: {})\n{}",
      id_.node_id, id_.session_id, msg_buf_len, msg.head().session_sequence(), msg.head().client_sequence(),
      msg.head().server_sequence(), protobuf_mini_dumper_get_readable(msg));

  write_actor_log_head(ctx, msg, msg_buf_len, false);

  return send_msg_to_client(buf_start, msg_buf_len);
}

int32_t session::send_msg_to_client(const void *msg_data, size_t msg_size) {
  // send data using dispatcher
  return cs_msg_dispatcher::me()->send_data(get_key().node_id, get_key().session_id, msg_data, msg_size);
}

int32_t session::broadcast_msg_to_client(uint64_t node_id, const atframework::CSMsg &msg) {
  size_t msg_buf_len = msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("broadcast to gateway [{:#x}] failed: require {}, only have {}", node_id, msg_buf_len, tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("broadcast msg to gateway [{:#x}] {} bytes\n{}", node_id, msg_buf_len,
             protobuf_mini_dumper_get_readable(msg));

  return broadcast_msg_to_client(node_id, buf_start, msg_buf_len);
}

int32_t session::broadcast_msg_to_client(uint64_t node_id, const void *msg_data, size_t msg_size) {
  // broadcast data using dispatcher
  return cs_msg_dispatcher::me()->broadcast_data(node_id, msg_data, msg_size);
}

bool session::compare_callback::operator()(const key_t &l, const key_t &r) const {
  if (l.node_id != r.node_id) {
    return l.session_id < r.session_id;
  }
  return l.node_id < r.node_id;
}

size_t session::compare_callback::operator()(const key_t &hash_obj) const {
  // std::hash also use fnv1 hash algorithm, but fnv1a sometime has better random
  return util::hash::hash_fnv1a<size_t>(&hash_obj.node_id, sizeof(hash_obj.node_id)) ^
         util::hash::hash_fnv1<size_t>(&hash_obj.session_id, sizeof(hash_obj.session_id));
}

void session::alloc_session_sequence(atframework::CSMsg &msg) {
  do {
    // has already alloc sequence, do nothing
    if (msg.head().session_sequence() != 0) {
      break;
    }

    // if the current player's session is no longer valid and
    // has not marked as no cache, sequence will be set to zero
    if (!is_valid()) {
      break;
    }

    msg.mutable_head()->set_session_sequence(++session_sequence_);
  } while (false);
}

void session::create_actor_log_writter() {
  if (!actor_log_writter_ && logic_config::me()->get_logic().session().actor_log_size() > 0 &&
      logic_config::me()->get_logic().session().actor_log_rotate() > 0) {
    actor_log_writter_ = util::log::log_wrapper::create_user_logger();
    if (actor_log_writter_) {
      actor_log_writter_->init(util::log::log_formatter::level_t::LOG_LW_INFO);
      actor_log_writter_->set_stacktrace_level(util::log::log_formatter::level_t::LOG_LW_DISABLED,
                                               util::log::log_formatter::level_t::LOG_LW_DISABLED);
      actor_log_writter_->set_prefix_format("[%F %T.%f]: ");

      std::stringstream ss_path;
      std::stringstream ss_alias;
      ss_path << logic_config::me()->get_logic().server().log_path() << "/cs-actor/%Y-%m-%d/" << cached_user_id_
              << ".%N.log";
      ss_alias << logic_config::me()->get_logic().server().log_path() << "/cs-actor/%Y-%m-%d/" << cached_user_id_
               << ".log";
      ::util::log::log_sink_file_backend file_sink(ss_path.str());
      file_sink.set_writing_alias_pattern(ss_alias.str());
      file_sink.set_flush_interval(1);  // flush every 1 second
      file_sink.set_max_file_size(logic_config::me()->get_logic().session().actor_log_size());
      file_sink.set_rotate_size(static_cast<uint32_t>(logic_config::me()->get_logic().session().actor_log_rotate()));
      actor_log_writter_->add_sink(file_sink);

      util::log::log_wrapper::caller_info_t caller = util::log::log_wrapper::caller_info_t(
          util::log::log_formatter::level_t::LOG_LW_INFO, {}, __FILE__, __LINE__, __FUNCTION__);
      actor_log_writter_->format_log(caller, "============ user: {}:{}, session: {:#x}:{} created ============",
                                     cached_zone_id_, cached_user_id_, get_key().node_id, get_key().session_id);
    }
  }

  if (!actor_log_otel_ && logic_config::me()->get_logic().session().enable_actor_otel_log()) {
    auto telemetry_group =
        rpc::telemetry::global_service::get_group(rpc::telemetry::semantic_conventions::kGroupNameCsActor);
    if (telemetry_group) {
      actor_log_otel_ = rpc::telemetry::global_service::get_current_default_logger(telemetry_group);
    }
  }
  if (actor_log_otel_) {
    std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue> attributes[] = {
        {"gateway.node_id", get_key().node_id},
        {"session.event", "create"},
        {"user.id", cached_user_id_},
        {"user.zone_id", cached_zone_id_},
        {opentelemetry::trace::SemanticConventions::kSessionId, get_key().session_id}};
    actor_log_otel_->Info(
        util::log::format("============ user: {}:{}, session: {:#x}:{}, client: {}:{} created ============",
                          cached_zone_id_, cached_user_id_, get_key().node_id, get_key().session_id),
        opentelemetry::common::MakeAttributes(attributes));
  }
}

int32_t session::send_kickoff(int32_t reason) {
  if (check_flag(flag_t::EN_SESSION_FLAG_GATEWAY_REMOVED)) {
    return 0;
  }
  // send kickoff using dispatcher
  return cs_msg_dispatcher::me()->send_kickoff(get_key().node_id, get_key().session_id, reason);
}

void session::write_actor_log_head(rpc::context &ctx, const atframework::CSMsg &msg, size_t byte_size, bool is_input) {
  if (!actor_log_writter_ && !actor_log_otel_) {
    return;
  }

  gsl::string_view rpc_name;
  gsl::string_view type_url;
  const atframework::CSMsgHead &head = msg.head();
  switch (head.rpc_type_case()) {
    case atframework::CSMsgHead::kRpcRequest:
      rpc_name = head.rpc_request().rpc_name();
      type_url = head.rpc_request().type_url();
      break;
    case atframework::CSMsgHead::kRpcResponse:
      rpc_name = head.rpc_response().rpc_name();
      type_url = head.rpc_response().type_url();
      break;
    case atframework::CSMsgHead::kRpcStream:
      rpc_name = head.rpc_stream().rpc_name();
      type_url = head.rpc_stream().type_url();
      break;
    default:
      rpc_name = "UNKNOWN RPC";
      type_url = "UNKNOWN TYPE";
      break;
  }

  util::log::log_wrapper::caller_info_t caller = util::log::log_wrapper::caller_info_t(
      util::log::log_formatter::level_t::LOG_LW_INFO, {}, __FILE__, __LINE__, __FUNCTION__);
  std::string hint_text;
  if (is_input) {
    hint_text = util::log::format(
        "<<<<<<<<<<<< receive {} bytes from player {}:{}, session: {:#x}:{}, rpc: {}, type: {}", byte_size,
        cached_zone_id_, cached_user_id_, get_key().node_id, get_key().session_id, rpc_name, type_url);

  } else {
    hint_text = util::log::format(">>>>>>>>>>>> send {} bytes to player {}:{}, session: {:#x}:{}, rpc: {}, type: {}",
                                  byte_size, cached_zone_id_, cached_user_id_, get_key().node_id, get_key().session_id,
                                  rpc_name, type_url);
  }
  if (actor_log_writter_) {
    actor_log_writter_->write_log(caller, hint_text.c_str(), hint_text.size());
  }
  if (actor_log_otel_) {
    std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue> attributes[] = {
        {"tconnd.node_id", get_key().node_id},
        {"session.event", is_input ? "receive_hint" : "send_hint"},
        {"user.id", cached_user_id_},
        {"user.zone_id", cached_zone_id_},
        {opentelemetry::trace::SemanticConventions::kRpcMethod,
         opentelemetry::nostd::string_view{rpc_name.data(), rpc_name.size()}},
        {opentelemetry::trace::SemanticConventions::kMessageId, head.client_sequence()},
        {opentelemetry::trace::SemanticConventions::kMessageType,
         opentelemetry::nostd::string_view{type_url.data(), type_url.size()}},
        {opentelemetry::trace::SemanticConventions::kMessageUncompressedSize, byte_size},
        {opentelemetry::trace::SemanticConventions::kSessionId, get_key().session_id}};
    if (ctx.get_trace_span()) {
      actor_log_otel_->Info(hint_text, opentelemetry::common::MakeAttributes(attributes),
                            ctx.get_trace_span()->GetContext());
    } else {
      actor_log_otel_->Info(hint_text, opentelemetry::common::MakeAttributes(attributes));
    }
  }
}

void session::write_actor_log_body(rpc::context &ctx, const google::protobuf::Message &msg,
                                   const atframework::CSMsgHead &head, bool is_input) {
  if (!actor_log_writter_ && !actor_log_otel_) {
    return;
  }

  gsl::string_view rpc_name;
  gsl::string_view type_url;
  switch (head.rpc_type_case()) {
    case atframework::CSMsgHead::kRpcRequest:
      rpc_name = head.rpc_request().rpc_name();
      type_url = head.rpc_request().type_url();
      break;
    case atframework::CSMsgHead::kRpcResponse:
      rpc_name = head.rpc_response().rpc_name();
      type_url = head.rpc_response().type_url();
      break;
    case atframework::CSMsgHead::kRpcStream:
      rpc_name = head.rpc_stream().rpc_name();
      type_url = head.rpc_stream().type_url();
      break;
    default:
      rpc_name = "UNKNOWN RPC";
      type_url = "UNKNOWN TYPE";
      break;
  }
  util::log::log_wrapper::caller_info_t caller = util::log::log_wrapper::caller_info_t(
      util::log::log_formatter::level_t::LOG_LW_INFO, {}, __FILE__, __LINE__, __FUNCTION__);

  std::string head_text = protobuf_mini_dumper_get_readable(head);
  std::string body_text = protobuf_mini_dumper_get_readable(msg);
  if (actor_log_writter_) {
    actor_log_writter_->format_log(caller,
                                   "============ session: {:#x}:{}, rpc: {}, type: {} ============\n------------ "
                                   "Head ------------\n{}------------ Body ------------\n{}",
                                   get_key().node_id, get_key().session_id, rpc_name, type_url, head_text, body_text);
  }
  if (actor_log_otel_) {
    std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue> attributes[] = {
        {"tconnd.node_id", get_key().node_id},

        {"user.id", cached_user_id_},
        {"user.zone_id", cached_zone_id_},
        {opentelemetry::trace::SemanticConventions::kRpcMethod,
         opentelemetry::nostd::string_view{rpc_name.data(), rpc_name.size()}},
        {opentelemetry::trace::SemanticConventions::kMessageId, head.client_sequence()},
        {opentelemetry::trace::SemanticConventions::kMessageType,
         opentelemetry::nostd::string_view{type_url.data(), type_url.size()}},
        {"message.result_code", head.error_code()},
        {"message.business_time", head.timestamp()},
        {opentelemetry::trace::SemanticConventions::kSessionId, get_key().session_id},
        {"session.event", is_input ? "receive_head" : "send_head"},
    };
    if (ctx.get_trace_span()) {
      actor_log_otel_->Info(head_text, opentelemetry::common::MakeAttributes(attributes),
                            ctx.get_trace_span()->GetContext());
    } else {
      actor_log_otel_->Info(head_text, opentelemetry::common::MakeAttributes(attributes));
    }

    attributes[std::extent<decltype(attributes)>::value - 1].second = is_input ? "receive_body" : "send_body";
    opentelemetry::nostd::string_view body_view =
        opentelemetry::nostd::string_view{body_text.c_str(), body_text.size()};
    if (body_view.empty()) {
      if (is_input) {
        body_view = "[EMPTY REQUEST]";
      } else if (head.error_code() < 0) {
        body_view = "[ERROR RESPONSE]";
      } else {
        body_view = "[EMPTY RESPONSE]";
      }
    }
    if (ctx.get_trace_span()) {
      actor_log_otel_->Info(body_view, opentelemetry::common::MakeAttributes(attributes),
                            ctx.get_trace_span()->GetContext());
    } else {
      actor_log_otel_->Info(body_view, opentelemetry::common::MakeAttributes(attributes));
    }
  }
}
