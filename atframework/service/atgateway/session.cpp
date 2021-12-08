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

namespace atframe {
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

session::session() : id_(0), router_(0), owner_(nullptr), flags_(0), peer_port_(0), private_data_(nullptr) {
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

session::ptr_t session::create(session_manager *mgr, std::unique_ptr<proto_base> &proto) {
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
    WLOGERROR("session 0x%p already closed or is closing, can not accept again", this);
    return error_code_t::EN_ECT_CLOSING;
  }

  if (check_flag(flag_t::EN_FT_HAS_FD)) {
    WLOGERROR("session 0x%p already has fd, can not accept again", this);
    return error_code_t::EN_ECT_ALREADY_HAS_FD;
  }

  int errcode = 0;
  if (0 != (errcode = uv_tcp_init(server->loop, &tcp_handle_))) {
    WLOGERROR("session 0x%p init tcp sock failed, error code: %d", this, errcode);
    return error_code_t::EN_ECT_NETWORK;
  }
  set_flag(flag_t::EN_FT_HAS_FD, true);

  if (0 != (errcode = uv_accept(server, &stream_handle_))) {
    WLOGERROR("session 0x%p accept tcp failed, error code: %d", this, errcode);
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
    WLOGERROR("session 0x%p already closed or is closing, can not accept again", this);
    return error_code_t::EN_ECT_CLOSING;
  }

  if (check_flag(flag_t::EN_FT_HAS_FD)) {
    WLOGERROR("session 0x%p already has fd, can not accept again", this);
    return error_code_t::EN_ECT_ALREADY_HAS_FD;
  }

  int errcode = 0;
  if (0 != (errcode = uv_pipe_init(server->loop, &unix_handle_, 1))) {
    WLOGERROR("session 0x%p init unix sock failed, error code: %d", this, errcode);
    return error_code_t::EN_ECT_NETWORK;
  }
  set_flag(flag_t::EN_FT_HAS_FD, true);

  if (0 != (errcode = uv_accept(server, &stream_handle_))) {
    WLOGERROR("session 0x%p accept unix failed, error code: %d", this, errcode);
    return error_code_t::EN_ECT_NETWORK;
  }

  uv_stream_set_blocking(&stream_handle_, 0);

  // get peer path
  char pipe_path[util::file_system::MAX_PATH_LEN] = {0};
  size_t path_len = sizeof(pipe_path);
  uv_pipe_getpeername(&unix_handle_, pipe_path, &path_len);
  peer_ip_.assign(pipe_path, path_len);
  peer_port_ = 0;

  return 0;
}

int session::init_new_session(::atbus::node::bus_id_t router) {
  static ::atframe::component::timestamp_id_allocator<id_t> id_alloc;
  // alloc id
  id_ = id_alloc.allocate();
  router_ = router;
  limit_.update_handshake_timepoint = util::time::time_utility::get_now() + owner_->get_conf().crypt.update_interval;

  set_flag(flag_t::EN_FT_INITED, true);
  return 0;
}

int session::init_reconnect(session &sess) {
  // copy id
  id_ = sess.id_;
  router_ = sess.router_;
  limit_ = sess.limit_;
  limit_.update_handshake_timepoint = util::time::time_utility::get_now() + owner_->get_conf().crypt.update_interval;

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
  ::atframe::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(id_);

  ::atframe::gw::ss_body_session *sess = msg.mutable_body()->mutable_add_session();
  if (nullptr != sess) {
    sess->set_client_ip(peer_ip_);
    sess->set_client_port(peer_port_);
  }

  int ret = send_to_server(msg);
  if (0 == ret) {
    set_flag(flag_t::EN_FT_REGISTERED, true);
    WLOGINFO("session 0x%llx send register notify to 0x%llx success", static_cast<unsigned long long>(id_),
             static_cast<unsigned long long>(router_));
  } else {
    WLOGERROR("session 0x%llx send register notify to 0x%llx failed, res: %d", static_cast<unsigned long long>(id_),
              static_cast<unsigned long long>(router_), ret);
  }

  return ret;
}

int session::send_remove_session() { return send_remove_session(owner_); }

int session::send_remove_session(session_manager *mgr) {
  if (!check_flag(flag_t::EN_FT_REGISTERED)) {
    return 0;
  }

  // send remove msg
  ::atframe::gw::ss_msg msg;
  msg.mutable_head()->set_session_id(id_);

  ::atframe::gw::ss_body_session *sess = msg.mutable_body()->mutable_remove_session();
  if (nullptr != sess) {
    sess->set_client_ip(get_peer_host());
    sess->set_client_port(get_peer_port());
  }

  int ret = send_to_server(msg, mgr);
  if (0 == ret) {
    set_flag(flag_t::EN_FT_REGISTERED, false);
    WLOGINFO("session 0x%llx send remove notify to 0x%llx success", static_cast<unsigned long long>(id_),
             static_cast<unsigned long long>(router_));
  } else {
    WLOGERROR("session 0x%llx send remove notify to 0x%llx failed, res: %d", static_cast<unsigned long long>(id_),
              static_cast<unsigned long long>(router_), ret);
  }

  return ret;
}

void session::on_alloc_read(size_t suggested_size, char *&out_buf, size_t &out_len) {
  if (proto_) {
    proto_->alloc_recv_buffer(suggested_size, out_buf, out_len);

    if (nullptr == out_buf && 0 == out_len) {
      close_fd(::atframe::gateway::close_reason_t::EN_CRT_INVALID_DATA);
    }
  }
}

void session::on_read(int ssz, const char *buff, size_t len) {
  if (proto_) {
    int errcode = 0;
    proto_->read(ssz, buff, len, errcode);

    if (errcode < 0) {
      WLOGERROR("session %s:%d read data length=%llu failed and will be closed, res: %d", peer_ip_.c_str(), peer_port_,
                static_cast<unsigned long long>(len), errcode);
      close(close_reason_t::EN_CRT_INVALID_DATA);
    }
  }
}

int session::on_write_done(int status) {
  if (proto_) {
    int ret = proto_->write_done(status);

    // if about to closing and all data transfered, shutdown the socket
    if (check_flag(flag_t::EN_FT_CLOSING_FD) && proto_->check_flag(proto_base::flag_t::EN_PFT_CLOSED)) {
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
    if (!proto_ || proto_->check_flag(proto_base::flag_t::EN_PFT_CLOSED)) {
      uv_shutdown(&shutdown_req_, &stream_handle_, on_evt_shutdown);
    }

    WLOGINFO("session 0x%llx(%p) lost fd", static_cast<unsigned long long>(id_), this);
  }

  return 0;
}

int session::send_to_client(const void *data, size_t len) {
  // send to proto_
  if (check_flag(flag_t::EN_FT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (!check_flag(flag_t::EN_FT_HAS_FD)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (!proto_) {
    WLOGERROR("sesseion %llx lost protocol handle when send to client", static_cast<unsigned long long>(id_));
    return error_code_t::EN_ECT_BAD_PROTOCOL;
  }

  // send limit
  limit_.hour_send_bytes += len;
  limit_.minute_send_bytes += len;
  limit_.total_send_bytes += len;
  ++limit_.total_send_times;
  ++limit_.hour_send_times;
  ++limit_.minute_send_times;

  int ret = proto_->write(data, len);

  check_hour_limit(false, true);
  check_minute_limit(false, true);
  check_total_limit(false, true);

  return ret;
}

int session::send_to_server(::atframe::gw::ss_msg &msg) { return send_to_server(msg, owner_); }

int session::send_to_server(::atframe::gw::ss_msg &msg, session_manager *mgr) {
  // send to router_
  if (0 == router_) {
    WLOGERROR("sesseion %llx has not configure router", static_cast<unsigned long long>(id_));
    return error_code_t::EN_ECT_INVALID_ROUTER;
  }

  if (nullptr == mgr) {
    mgr = owner_;
  }

  if (nullptr == mgr) {
    WLOGERROR("sesseion %llx has lost manager and can not send ss message any more",
              static_cast<unsigned long long>(id_));
    return error_code_t::EN_ECT_LOST_MANAGER;
  }

  // send to server with type = ::atframe::component::service_type::EN_ATST_GATEWAY
  std::string packed_buffer;
  if (false == msg.SerializeToString(&packed_buffer)) {
    WLOGERROR("sesseion %llx serialize failed and can not send ss message: %s", static_cast<unsigned long long>(id_),
              msg.InitializationErrorString().c_str());
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

  int ret = mgr->post_data(router_, ::atframe::component::service_type::EN_ATST_GATEWAY, packed_buffer.data(), len);

  check_hour_limit(true, false);
  check_minute_limit(true, false);
  check_total_limit(true, false);

  return ret;
}

proto_base *session::get_protocol_handle() { return proto_.get(); }
const proto_base *session::get_protocol_handle() const { return proto_.get(); }

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
  time_t now_hr = ::util::time::time_utility::get_now() / ::util::time::time_utility::DAY_SECONDS;
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
  time_t now_mi = ::util::time::time_utility::get_now() / ::util::time::time_utility::MINITE_SECONDS;
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
    if (limit_.update_handshake_timepoint < ::util::time::time_utility::get_now()) {
      limit_.update_handshake_timepoint =
          ::util::time::time_utility::get_now() + owner_->get_conf().crypt.update_interval;
      proto_base *proto = get_protocol_handle();
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
}  // namespace atframe
