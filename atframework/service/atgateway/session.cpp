// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "session.h"

#include <uv.h>

#include <common/file_system.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler_features.h>

#include <sstream>
#include <type_traits>

#include "config/atframe_service_types.h"
#include "core/timestamp_id_allocator.h"

#include "session_manager.h"

namespace atframework {
namespace gateway {
#if defined(UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT) && UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT
#  if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201402L)) ||                       \
      (defined(__cplusplus) && __cplusplus >= 201402L &&                        \
       !(!defined(__clang__) && defined(__GNUC__) && defined(__GNUC_MINOR__) && \
         __GNUC__ * 100 + __GNUC_MINOR__ <= 409))
UTIL_CONFIG_STATIC_ASSERT(std::is_trivially_copyable<session::limit_t>::value);
#  elif (defined(__cplusplus) && __cplusplus >= 201103L) || ((defined(_MSVC_LANG) && _MSVC_LANG >= 201103L))
UTIL_CONFIG_STATIC_ASSERT(std::is_trivial<session::limit_t>::value);
#  else
UTIL_CONFIG_STATIC_ASSERT(std::is_pod<session::limit_t>::value);
#  endif
#endif

session::session() : id_(0), router_node_id_(0), owner_(nullptr), flags_(0), peer_port_(0), private_data_(nullptr) {
  memset(&limit_, 0, sizeof(limit_));
  raw_handle_.data = this;
}

session::~session() { assert(check_flag(flag_t::EN_FT_CLOSING)); }

bool session::check_flag(flag_t::type t) const { return 0 != (flags_ & t); }

void session::set_flag(flag_t::type t, bool v) {
  if (v) {
    flags_ |= t;
  } else {
    flags_ &= ~t;
  }
}

session::ptr_t session::create(session_manager *mgr,
                               std::unique_ptr<atframework::gateway::libatgw_protocol_api> &proto) {
  ptr_t ret = std::make_shared<session>();
  if (!ret) {
    return ret;
  }

  ret->owner_ = mgr;
  ret->proto_.swap(proto);

  if (!ret->proto_) {
    return ptr_t();
  }

  ret->proto_->set_private_data(ret.get());
  return ret;
}

int session::accept_tcp(uv_stream_t *server) {
  if (check_flag(flag_t::EN_FT_CLOSING)) {
    FWLOGERROR("session {} already closed or is closing, can not accept again", reinterpret_cast<const void *>(this));
    return error_code_t::EN_ECT_CLOSING;
  }

  if (check_flag(flag_t::EN_FT_HAS_FD)) {
    FWLOGERROR("session {} already has fd, can not accept again", reinterpret_cast<const void *>(this));
    return error_code_t::EN_ECT_ALREADY_HAS_FD;
  }

  int errcode = 0;
  if (0 != (errcode = uv_tcp_init(server->loop, &tcp_handle_))) {
    FWLOGERROR("session {} init tcp sock failed, error code: {}", reinterpret_cast<const void *>(this), errcode);
    return error_code_t::EN_ECT_NETWORK;
  }
  set_flag(flag_t::EN_FT_HAS_FD, true);

  if (0 != (errcode = uv_accept(server, &stream_handle_))) {
    FWLOGERROR("session {} accept tcp failed, error code: {}", reinterpret_cast<const void *>(this), errcode);
    return error_code_t::EN_ECT_NETWORK;
  }

  uv_tcp_nodelay(&tcp_handle_, 1);
  uv_stream_set_blocking(&stream_handle_, 0);

  // get peer ip&port
  sockaddr_storage sock_addr;
  int name_len = sizeof(sock_addr);
  uv_tcp_getpeername(&tcp_handle_, reinterpret_cast<struct sockaddr *>(&sock_addr), &name_len);

  char ip[64] = {0};
  if (sock_addr.ss_family == AF_INET6) {
    sockaddr_in6 *sock_addr_ipv6 = reinterpret_cast<struct sockaddr_in6 *>(&sock_addr);
    uv_ip6_name(sock_addr_ipv6, ip, sizeof(ip));
    peer_ip_ = ip;
    peer_port_ = static_cast<int32_t>(sock_addr_ipv6->sin6_port);
  } else {
    sockaddr_in *sock_addr_ipv4 = reinterpret_cast<struct sockaddr_in *>(&sock_addr);
    uv_ip4_name(sock_addr_ipv4, ip, sizeof(ip));
    peer_ip_ = ip;
    peer_port_ = static_cast<int32_t>(sock_addr_ipv4->sin_port);
  }

  return 0;
}

int session::accept_pipe(uv_stream_t *server) {
  if (check_flag(flag_t::EN_FT_CLOSING)) {
    FWLOGERROR("session {} already closed or is closing, can not accept again", reinterpret_cast<const void *>(this));
    return error_code_t::EN_ECT_CLOSING;
  }

  if (check_flag(flag_t::EN_FT_HAS_FD)) {
    FWLOGERROR("session {} already has fd, can not accept again", reinterpret_cast<const void *>(this));
    return error_code_t::EN_ECT_ALREADY_HAS_FD;
  }

  int errcode = 0;
  if (0 != (errcode = uv_pipe_init(server->loop, &unix_handle_, 1))) {
    FWLOGERROR("session {} init unix sock failed, error code: {}", reinterpret_cast<const void *>(this), errcode);
    return error_code_t::EN_ECT_NETWORK;
  }
  set_flag(flag_t::EN_FT_HAS_FD, true);

  if (0 != (errcode = uv_accept(server, &stream_handle_))) {
    FWLOGERROR("session {} accept unix failed, error code: {}", reinterpret_cast<const void *>(this), errcode);
    return error_code_t::EN_ECT_NETWORK;
  }

  uv_stream_set_blocking(&stream_handle_, 0);

  // get peer path
  char pipe_path[util::file_system::MAX_PATH_LEN];
  size_t path_len = sizeof(pipe_path);
  uv_pipe_getpeername(&unix_handle_, pipe_path, &path_len);
  if (path_len < sizeof(pipe_path)) {
    pipe_path[path_len] = 0;
  } else {
    pipe_path[sizeof(pipe_path) - 1] = 0;
  }
  peer_ip_.assign(pipe_path, path_len);
  peer_port_ = 0;

  return 0;
}

int session::init_new_session() {
  static ::atframework::component::timestamp_id_allocator<id_t> id_alloc;
  // alloc id
  id_ = id_alloc.allocate();
  router_node_id_ = 0;
  router_node_name_.clear();
  limit_.update_handshake_timepoint =
      atfw::util::time::time_utility::get_now() + owner_->get_conf().crypt.update_interval;

  set_flag(flag_t::EN_FT_INITED, true);
  return 0;
}

int session::init_reconnect(session &sess) {
  // copy id
  id_ = sess.id_;
  router_node_id_ = sess.router_node_id_;
  router_node_name_ = sess.router_node_name_;
  limit_ = sess.limit_;
  limit_.update_handshake_timepoint =
      atfw::util::time::time_utility::get_now() + owner_->get_conf().crypt.update_interval;

  private_data_ = sess.private_data_;

  set_flag(flag_t::EN_FT_INITED, true);
  set_flag(flag_t::EN_FT_REGISTERED, sess.check_flag(flag_t::EN_FT_REGISTERED));

  sess.set_flag(flag_t::EN_FT_RECONNECTED, true);
  sess.set_flag(session::flag_t::EN_FT_WAIT_RECONNECT, false);
  return 0;
}

int session::send_new_session() {
  if (check_flag(flag_t::EN_FT_REGISTERED)) {
    return 0;
  }

  // send new msg
  ::atframework::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(id_);

  ::atframework::gw::ss_body_session *sess = msg.mutable_body()->mutable_add_session();
  if (nullptr != sess) {
    sess->set_client_ip(peer_ip_);
    sess->set_client_port(peer_port_);
  }

  int ret = send_to_server(msg);
  if (0 == ret) {
    set_flag(flag_t::EN_FT_REGISTERED, true);
    FWLOGINFO("session {} send register notify to {}({}) success", id_, router_node_id_, router_node_name_);
  } else {
    FWLOGERROR("session {} send register notify to {}({}) failed, res: {}", id_, router_node_id_, router_node_name_,
               ret);
  }

  return ret;
}

int session::send_remove_session() { return send_remove_session(owner_); }

int session::send_remove_session(session_manager *mgr) {
  if (!check_flag(flag_t::EN_FT_REGISTERED)) {
    return 0;
  }

  // send remove msg
  ::atframework::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(id_);

  ::atframework::gw::ss_body_session *sess = msg.mutable_body()->mutable_remove_session();
  if (nullptr != sess) {
    sess->set_client_ip(get_peer_host());
    sess->set_client_port(get_peer_port());
  }

  int ret = send_to_server(msg, mgr);
  if (0 == ret) {
    set_flag(flag_t::EN_FT_REGISTERED, false);
    FWLOGINFO("session {} send remove notify to {}({}) success", id_, router_node_id_, router_node_name_);
  } else {
    FWLOGERROR("session {} send remove notify to {}({}) failed, res: {}", id_, router_node_id_, router_node_name_, ret);
  }

  return ret;
}

void session::on_alloc_read(size_t suggested_size, char *&out_buf, size_t &out_len) {
  if (proto_) {
    proto_->alloc_recv_buffer(suggested_size, out_buf, out_len);

    if (nullptr == out_buf && 0 == out_len) {
      close_fd(::atframework::gateway::close_reason_t::EN_CRT_INVALID_DATA);
    }
  }
}

void session::on_read(int ssz, gsl::span<const unsigned char> buffer) {
  if (proto_) {
    int errcode = 0;
    proto_->read(ssz, buffer, errcode);

    if (errcode < 0) {
      FWLOGERROR("session {}:{} read data length={} failed and will be closed, res: {}", peer_ip_, peer_port_,
                 buffer.size(), errcode);
      close(close_reason_t::EN_CRT_INVALID_DATA);
    }
  }
}

int session::on_write_done(int status) {
  if (proto_) {
    int ret = proto_->write_done(status);

    // if about to closing and all data transferred, shutdown the socket
    if (check_flag(flag_t::EN_FT_CLOSING_FD) &&
        proto_->check_flag(atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_CLOSED)) {
      uv_shutdown(&shutdown_req_, &stream_handle_, on_evt_shutdown);
    }

    return ret;
  }

  return 0;
}

int session::close(int reason) { return close_with_manager(reason, owner_); }

int session::close_with_manager(int reason, session_manager *mgr) {
  // 这个接口会被多次调用（分别在关闭网络连接、重连超时、主动踢下线）
  // 重连超时的逻辑不会走后面的流程了，但是还是要通知服务器踢下线
  if (check_flag(flag_t::EN_FT_REGISTERED) && !check_flag(flag_t::EN_FT_RECONNECTED) &&
      !check_flag(flag_t::EN_FT_WAIT_RECONNECT)) {
    send_remove_session(mgr);
  }

  if (check_flag(flag_t::EN_FT_CLOSING)) {
    return 0;
  }

  set_flag(flag_t::EN_FT_CLOSING, true);
  return close_fd(reason);
}

int session::close_fd(int reason) {
  if (check_flag(flag_t::EN_FT_CLOSING_FD)) {
    return 0;
  }

  if (check_flag(flag_t::EN_FT_HAS_FD)) {
    set_flag(flag_t::EN_FT_HAS_FD, false);

    if (proto_) {
      proto_->close(reason);
    }

    // shutdown and close uv_stream_t
    // manager can not be used any more
    owner_ = nullptr;
    shutdown_req_.data = new ptr_t(shared_from_this());

    // if writing, wait all data written an then shutdown it
    set_flag(flag_t::EN_FT_CLOSING_FD, true);
    if (!proto_ || proto_->check_flag(atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_CLOSED)) {
      uv_shutdown(&shutdown_req_, &stream_handle_, on_evt_shutdown);
    }

    FWLOGINFO("session {}({}) lost fd", id_, reinterpret_cast<const void *>(this));
  }

  return 0;
}

int session::send_to_client(gsl::span<const unsigned char> data) {
  // send to proto_
  if (check_flag(flag_t::EN_FT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (!check_flag(flag_t::EN_FT_HAS_FD)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (!proto_) {
    FWLOGERROR("session {} lost protocol handle when send to client", id_);
    return error_code_t::EN_ECT_BAD_PROTOCOL;
  }

  // send limit
  limit_.hour_send_bytes += data.size();
  limit_.minute_send_bytes += data.size();
  limit_.total_send_bytes += data.size();
  ++limit_.total_send_times;
  ++limit_.hour_send_times;
  ++limit_.minute_send_times;

  int ret = proto_->write(data);

  check_hour_limit(false, true);
  check_minute_limit(false, true);
  check_total_limit(false, true);

  return ret;
}

int session::send_to_server(::atframework::gw::ss_msg &msg) { return send_to_server(msg, owner_); }

int session::send_to_server(::atframework::gw::ss_msg &msg, session_manager *mgr) {
  // send to router_
  if (0 == router_node_id_ && router_node_name_.empty()) {
    FWLOGERROR("session {} has not configure router", id_);
    return error_code_t::EN_ECT_INVALID_ROUTER;
  }

  if (nullptr == mgr) {
    mgr = owner_;
  }

  if (nullptr == mgr) {
    FWLOGERROR("session {} has lost manager and can not send ss message any more", id_);
    return error_code_t::EN_ECT_LOST_MANAGER;
  }

  // send to server with type = ::atframework::component::service_type::EN_ATST_GATEWAY
  std::string packed_buffer;
  if (false == msg.SerializeToString(&packed_buffer)) {
    FWLOGERROR("session {} serialize failed and can not send ss message: {}", id_, msg.InitializationErrorString());
    return error_code_t::EN_ECT_BAD_DATA;
  }

  size_t len = packed_buffer.size();
  // recv limit
  limit_.hour_recv_bytes += len;
  limit_.minute_recv_bytes += len;
  limit_.total_recv_bytes += len;
  ++limit_.minute_recv_times;
  ++limit_.hour_recv_times;
  ++limit_.total_recv_times;

  int ret;
  if (0 != router_node_id_) {
    ret = mgr->post_data(
        router_node_id_, ::atframework::component::service_type::EN_ATST_GATEWAY,
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed_buffer.data()), len});
  } else {
    ret = mgr->post_data(
        router_node_name_, ::atframework::component::service_type::EN_ATST_GATEWAY,
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed_buffer.data()), len});
  }

  check_hour_limit(true, false);
  check_minute_limit(true, false);
  check_total_limit(true, false);

  return ret;
}

atframework::gateway::libatgw_protocol_api *session::get_protocol_handle() { return proto_.get(); }
const atframework::gateway::libatgw_protocol_api *session::get_protocol_handle() const { return proto_.get(); }

uv_stream_t *session::get_uv_stream() { return &stream_handle_; }
const uv_stream_t *session::get_uv_stream() const { return &stream_handle_; }

void session::on_evt_shutdown(uv_shutdown_t *req, int /*status*/) {
  // call close API
  session *self = reinterpret_cast<session *>(req->handle->data);
  assert(self);

  uv_close(&self->raw_handle_, on_evt_closed);
}

void session::on_evt_closed(uv_handle_t *handle) {
  assert(handle && handle->data);
  if (nullptr == handle || nullptr == handle->data) {
    return;
  }

  session *self = reinterpret_cast<session *>(handle->data);
  assert(self);
  self->set_flag(flag_t::EN_FT_CLOSING_FD, false);

  // free session object
  ptr_t *holder = reinterpret_cast<ptr_t *>(self->shutdown_req_.data);
  assert(holder);
  delete holder;
}

void session::check_hour_limit(bool check_recv, bool check_send) {
  time_t now_hr = atfw::util::time::time_utility::get_now() / atfw::util::time::time_utility::DAY_SECONDS;
  if (now_hr != limit_.hour_timepoint) {
    limit_.hour_timepoint = now_hr;
    limit_.hour_recv_bytes = 0;
    limit_.hour_send_bytes = 0;
    limit_.hour_recv_times = 0;
    limit_.hour_send_times = 0;
    return;
  }

  if (nullptr == owner_) {
    return;
  }

  if (check_flag(flag_t::EN_FT_CLOSING)) {
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().hour_recv_bytes() > 0 &&
      limit_.hour_recv_bytes > owner_->get_conf().origin_conf.client().limit().hour_recv_bytes()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().hour_send_bytes() > 0 &&
      limit_.hour_send_bytes > owner_->get_conf().origin_conf.client().limit().hour_send_bytes()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().hour_recv_times() > 0 &&
      limit_.hour_recv_times > owner_->get_conf().origin_conf.client().limit().hour_recv_times()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().hour_send_times() > 0 &&
      limit_.hour_send_times > owner_->get_conf().origin_conf.client().limit().hour_send_times()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }
}

void session::check_minute_limit(bool check_recv, bool check_send) {
  time_t now_mi = atfw::util::time::time_utility::get_now() / atfw::util::time::time_utility::MINITE_SECONDS;
  if (now_mi != limit_.minute_timepoint) {
    limit_.minute_timepoint = now_mi;
    limit_.minute_recv_bytes = 0;
    limit_.minute_send_bytes = 0;
    limit_.minute_recv_times = 0;
    limit_.minute_send_times = 0;
    return;
  }

  if (nullptr == owner_) {
    return;
  }

  if (check_flag(flag_t::EN_FT_CLOSING)) {
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().minute_recv_bytes() > 0 &&
      limit_.minute_recv_bytes > owner_->get_conf().origin_conf.client().limit().minute_recv_bytes()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().minute_recv_times() > 0 &&
      limit_.minute_recv_times > owner_->get_conf().origin_conf.client().limit().minute_recv_times()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
    return;
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().minute_send_bytes() > 0 &&
      limit_.minute_send_bytes > owner_->get_conf().origin_conf.client().limit().minute_send_bytes()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
    return;
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().minute_send_times() > 0 &&
      limit_.minute_send_times > owner_->get_conf().origin_conf.client().limit().minute_send_times()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
    return;
  }

  if (nullptr != owner_ && owner_->get_conf().crypt.update_interval > 0 && check_flag(flag_t::EN_FT_HAS_FD)) {
    if (limit_.update_handshake_timepoint < atfw::util::time::time_utility::get_now()) {
      limit_.update_handshake_timepoint =
          atfw::util::time::time_utility::get_now() + owner_->get_conf().crypt.update_interval;
      atframework::gateway::libatgw_protocol_api *proto = get_protocol_handle();
      if (nullptr != proto) {
        proto->handshake_update();
      }
    }
  }
}

void session::check_total_limit(bool check_recv, bool check_send) {
  if (nullptr == owner_) {
    return;
  }

  if (check_flag(flag_t::EN_FT_CLOSING)) {
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().total_recv_bytes() > 0 &&
      limit_.total_recv_bytes > owner_->get_conf().origin_conf.client().limit().total_send_bytes()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().total_recv_times() > 0 &&
      limit_.total_recv_times > owner_->get_conf().origin_conf.client().limit().total_recv_times()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().total_send_bytes() > 0 &&
      limit_.total_send_bytes > owner_->get_conf().origin_conf.client().limit().total_send_bytes()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().total_send_times() > 0 &&
      limit_.total_send_times > owner_->get_conf().origin_conf.client().limit().total_send_times()) {
    close(close_reason_t::EN_CRT_TRAFIC_EXTENDED);
  }
}
}  // namespace gateway
}  // namespace atframework
