// Copyright 2021 atframework

#include "data/session.h"

#include <algorithm/hash.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/cs_msg_dispatcher.h>
#include <proto_base.h>

#include <utility/environment_helper.h>
#include <utility/protobuf_mini_dumper.h>

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

session::~session() { FWLOGDEBUG("session [{:#x}, {}] destroyed", id_.bus_id, id_.session_id); }

bool session::is_closing() const noexcept { return check_flag(flag_t::EN_SESSION_FLAG_CLOSING); }

bool session::is_closed() const noexcept { return check_flag(flag_t::EN_SESSION_FLAG_CLOSED); }

bool session::is_valid() const noexcept {
  return 0 == (flags_ & (flag_t::EN_SESSION_FLAG_CLOSING | flag_t::EN_SESSION_FLAG_CLOSED |
                         flag_t::EN_SESSION_FLAG_GATEWAY_REMOVED));
}

void session::set_player(std::shared_ptr<player_cache> u) { player_ = u; }

std::shared_ptr<player_cache> session::get_player() const { return player_.lock(); }

int32_t session::send_msg_to_client(PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &msg) {
  if (0 == msg.head().server_sequence()) {
    std::shared_ptr<player_cache> user = get_player();
    if (user) {
      return send_msg_to_client(msg, user->alloc_server_sequence());
    }
  }
  return send_msg_to_client(msg, msg.head().server_sequence());
}

int32_t session::send_msg_to_client(PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &msg, uint64_t server_sequence) {
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
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("send msg to client:[{:#x}, {}] {} bytes\n{}", id_.bus_id, id_.session_id, msg_buf_len,
             protobuf_mini_dumper_get_readable(msg));

  return send_msg_to_client(buf_start, msg_buf_len);
}

int32_t session::send_msg_to_client(const void *msg_data, size_t msg_size) {
  // send data using dispatcher
  return cs_msg_dispatcher::me()->send_data(get_key().bus_id, get_key().session_id, msg_data, msg_size);
}

int32_t session::broadcast_msg_to_client(uint64_t bus_id, const PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &msg) {
  size_t msg_buf_len = msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("broadcast to gateway [{:#x}] failed: require {}, only have {}", bus_id, msg_buf_len, tls_buf_len);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
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

void session::alloc_session_sequence(PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &msg) {
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
