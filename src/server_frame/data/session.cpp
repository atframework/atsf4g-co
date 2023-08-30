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

#include <utility>

#include "data/player_cache.h"

session::key_t::key_t() : bus_id(0), session_id(0) {}
session::key_t::key_t(const std::pair<uint64_t, uint64_t> &p) : bus_id(p.first), session_id(p.second) {}

bool session::key_t::operator==(const key_t &r) const { return bus_id == r.bus_id && session_id == r.session_id; }

bool session::key_t::operator!=(const key_t &r) const { return !((*this) == r); }

bool session::key_t::operator<(const key_t &r) const {
  if (bus_id != r.bus_id) {
    return bus_id < r.bus_id;
  }
  return session_id < r.session_id;
}

bool session::key_t::operator<=(const key_t &r) const { return (*this) < r || (*this) == r; }

bool session::key_t::operator>(const key_t &r) const {
  if (bus_id != r.bus_id) {
    return bus_id > r.bus_id;
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

session::session() : flags_(0), login_task_id_(0), session_sequence_(0) {
  id_.bus_id = 0;
  id_.session_id = 0;
}

session::~session() {
  FWLOGDEBUG("session [{:#x}, {}] destroyed", id_.bus_id, id_.session_id);

  if (actor_log_writter_) {
    util::log::log_wrapper::caller_info_t caller = util::log::log_wrapper::caller_info_t(
        util::log::log_formatter::level_t::LOG_LW_INFO, NULL, __FILE__, __LINE__, __FUNCTION__);
    actor_log_writter_->format_log(caller, "------------ session: {:#x}:{} destroyed ------------", get_key().bus_id,
                                   get_key().session_id);
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

  if (u && !actor_log_writter_ && logic_config::me()->get_logic().session().enable_actor_log() &&
      logic_config::me()->get_logic().session().actor_log_size() > 0 &&
      logic_config::me()->get_logic().session().actor_log_rotate() > 0) {
    actor_log_writter_ = util::log::log_wrapper::create_user_logger();
    if (actor_log_writter_) {
      actor_log_writter_->init(util::log::log_formatter::level_t::LOG_LW_INFO);
      actor_log_writter_->set_stacktrace_level(util::log::log_formatter::level_t::LOG_LW_DISABLED,
                                               util::log::log_formatter::level_t::LOG_LW_DISABLED);
      actor_log_writter_->set_prefix_format("[%F %T.%f]: ");

      std::stringstream ss_path;
      std::stringstream ss_alias;
      ss_path << logic_config::me()->get_logic().server().log_path() << "/cs-actor/%Y-%m-%d/" << u->get_user_id()
              << ".%N.log";
      ss_alias << logic_config::me()->get_logic().server().log_path() << "/cs-actor/%Y-%m-%d/" << u->get_user_id()
               << ".log";
      ::util::log::log_sink_file_backend file_sink(ss_path.str());
      file_sink.set_writing_alias_pattern(ss_alias.str());
      file_sink.set_flush_interval(1);  // flush every 1 second
      file_sink.set_max_file_size(logic_config::me()->get_logic().session().actor_log_size());
      file_sink.set_rotate_size(static_cast<uint32_t>(logic_config::me()->get_logic().session().actor_log_rotate()));
      actor_log_writter_->add_sink(file_sink);

      util::log::log_wrapper::caller_info_t caller = util::log::log_wrapper::caller_info_t(
          util::log::log_formatter::level_t::LOG_LW_INFO, NULL, __FILE__, __LINE__, __FUNCTION__);
      actor_log_writter_->format_log(caller, "============ user id: {}, session: {:#x}:{} created ============",
                                     u->get_user_id(), get_key().bus_id, get_key().session_id);
    }
  }
}

std::shared_ptr<player_cache> session::get_player() const { return player_.lock(); }

int32_t session::send_msg_to_client(atframework::CSMsg &msg) {
  if (0 == msg.head().server_sequence()) {
    std::shared_ptr<player_cache> user = get_player();
    if (user) {
      return send_msg_to_client(msg, user->alloc_server_sequence());
    }
  }
  return send_msg_to_client(msg, msg.head().server_sequence());
}

int32_t session::send_msg_to_client(atframework::CSMsg &msg, uint64_t server_sequence) {
  if (!msg.has_head() || msg.head().timestamp() == 0) {
    msg.mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  msg.mutable_head()->set_server_sequence(server_sequence);
  alloc_session_sequence(msg);

  size_t msg_buf_len = msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("send to gateway [{:#x}, {}] failed: require {}, only have {}", id_.bus_id, id_.session_id, msg_buf_len,
               tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG(
      "send msg to client:[{:#x}, {}] {} bytes.(session sequence: {}, client sequence: {}, server sequence: {})\n{}",
      id_.bus_id, id_.session_id, msg_buf_len, msg.head().session_sequence(), msg.head().client_sequence(),
      msg.head().server_sequence(), protobuf_mini_dumper_get_readable(msg));

  return send_msg_to_client(buf_start, msg_buf_len);
}

int32_t session::send_msg_to_client(const void *msg_data, size_t msg_size) {
  // send data using dispatcher
  return cs_msg_dispatcher::me()->send_data(get_key().bus_id, get_key().session_id, msg_data, msg_size);
}

int32_t session::broadcast_msg_to_client(uint64_t bus_id, const atframework::CSMsg &msg) {
  size_t msg_buf_len = msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("broadcast to gateway [{:#x}] failed: require {}, only have {}", bus_id, msg_buf_len, tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("broadcast msg to gateway [{:#x}] {} bytes\n{}", bus_id, msg_buf_len,
             protobuf_mini_dumper_get_readable(msg));

  return broadcast_msg_to_client(bus_id, buf_start, msg_buf_len);
}

int32_t session::broadcast_msg_to_client(uint64_t bus_id, const void *msg_data, size_t msg_size) {
  // broadcast data using dispatcher
  return cs_msg_dispatcher::me()->broadcast_data(bus_id, msg_data, msg_size);
}

bool session::compare_callback::operator()(const key_t &l, const key_t &r) const {
  if (l.bus_id != r.bus_id) {
    return l.session_id < r.session_id;
  }
  return l.bus_id < r.bus_id;
}

size_t session::compare_callback::operator()(const key_t &hash_obj) const {
  // std::hash also use fnv1 hash algorithm, but fnv1a sometime has better random
  return util::hash::hash_fnv1a<size_t>(&hash_obj.bus_id, sizeof(hash_obj.bus_id)) ^
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

int32_t session::send_kickoff(int32_t reason) {
  if (check_flag(flag_t::EN_SESSION_FLAG_GATEWAY_REMOVED)) {
    return 0;
  }
  // send kickoff using dispatcher
  return cs_msg_dispatcher::me()->send_kickoff(get_key().bus_id, get_key().session_id, reason);
}

void session::write_actor_log_head(const atframework::CSMsg &msg, size_t byte_size, bool is_input) {
  ::util::log::log_wrapper *writter = mutable_actor_log_writter();
  if (nullptr == writter) {
    return;
  }

  uint64_t player_user_id = 0;
  uint32_t player_zone_id = 0;
  {
    std::shared_ptr<player_cache> user = get_player();
    if (user) {
      player_user_id = user->get_user_id();
      player_zone_id = user->get_zone_id();
    }
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
      util::log::log_formatter::level_t::LOG_LW_INFO, NULL, __FILE__, __LINE__, __FUNCTION__);
  if (is_input) {
    writter->format_log(caller, "<<<<<<<<<<<< receive {} bytes from player {}:{}, session: {:#x}:{}, rpc: {}, type: {}",
                        byte_size, player_zone_id, player_user_id, get_key().bus_id, get_key().session_id, rpc_name,
                        type_url);
  } else {
    writter->format_log(caller, ">>>>>>>>>>>> send {} bytes to player {}:{}, session: {:#x}:{}, rpc: {}, type: {}",
                        byte_size, player_zone_id, player_user_id, get_key().bus_id, get_key().session_id, rpc_name,
                        type_url);
  }
}

void session::write_actor_log_body(const google::protobuf::Message &msg, const atframework::CSMsgHead &head) {
  ::util::log::log_wrapper *writter = mutable_actor_log_writter();
  if (nullptr == writter) {
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
      util::log::log_formatter::level_t::LOG_LW_INFO, NULL, __FILE__, __LINE__, __FUNCTION__);
  writter->format_log(caller,
                      "============ session: {:#x}:{}, rpc: {}, type: {} ============\n------------ "
                      "Head ------------\n{}------------ Body ------------\n{}",
                      get_key().bus_id, get_key().session_id, rpc_name, type_url,
                      protobuf_mini_dumper_get_readable(head), protobuf_mini_dumper_get_readable(msg));
}

::util::log::log_wrapper *session::mutable_actor_log_writter() { return actor_log_writter_.get(); }
