// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "session.h"

#include <uv.h>

#include <common/file_system.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler_features.h>

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

session::session()
    : id_(0),
      router_node_id_(0),
      owner_(nullptr),
      limit_{},
      flags_(0),
      raw_handle_(),
      shutdown_req_(),
      peer_port_(0),
      private_data_(nullptr) {
  raw_handle_.data = this;
}

session::~session() { assert(check_flag(flag_t::kClosing)); }

bool session::check_flag(flag_t t) const { return 0 != (flags_ & static_cast<uint32_t>(t)); }

void session::set_flag(flag_t t, bool v) {
  const uint32_t flag_value = static_cast<uint32_t>(t);
  if (v) {
    flags_ |= flag_value;
  } else {
    flags_ &= ~flag_value;
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
    return {};
  }

  ret->proto_->set_private_data(ret.get());
  return ret;
}

int session::accept_tcp(uv_stream_t *server) {
  if (check_flag(flag_t::kClosing)) {
    FWLOGERROR("{} already closed or is closing, can not accept again", *this);
    return static_cast<int>(error_code_t::kClosing);
  }

  if (check_flag(flag_t::kHasFd)) {
    FWLOGERROR("{} already has fd, can not accept again", *this);
    return static_cast<int>(error_code_t::kAlreadyHasFd);
  }

  int errcode = uv_tcp_init(server->loop, &tcp_handle_);
  if (0 != errcode) {
    FWLOGERROR("{} init tcp sock failed, error code: {}", *this, errcode);
    return static_cast<int>(error_code_t::kNetwork);
  }
  set_flag(flag_t::kHasFd, true);

  errcode = uv_accept(server, &stream_handle_);
  if (0 != errcode) {
    FWLOGERROR("{} accept tcp failed, error code: {}", *this, errcode);
    return static_cast<int>(error_code_t::kNetwork);
  }

  uv_tcp_nodelay(&tcp_handle_, 1);
  uv_stream_set_blocking(&stream_handle_, 0);

  // get peer ip&port
  sockaddr_storage sock_addr = {};
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
  if (check_flag(flag_t::kClosing)) {
    FWLOGERROR("{} already closed or is closing, can not accept again", *this);
    return static_cast<int>(error_code_t::kClosing);
  }

  if (check_flag(flag_t::kHasFd)) {
    FWLOGERROR("{} already has fd, can not accept again", *this);
    return static_cast<int>(error_code_t::kAlreadyHasFd);
  }

  int errcode = uv_pipe_init(server->loop, &unix_handle_, 1);
  if (0 != errcode) {
    FWLOGERROR("{} init unix sock failed, error code: {}", *this, errcode);
    return static_cast<int>(error_code_t::kNetwork);
  }
  set_flag(flag_t::kHasFd, true);

  errcode = uv_accept(server, &stream_handle_);
  if (0 != errcode) {
    FWLOGERROR("{} accept unix failed, error code: {}", *this, errcode);
    return static_cast<int>(error_code_t::kNetwork);
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

  set_flag(flag_t::kInited, true);
  FWLOGWARNING("{} new session inited", *this);
  return 0;
}

int session::init_reconnect(session &sess) {
  // copy id
  id_ = sess.id_;
  router_node_id_ = sess.router_node_id_;
  router_node_name_ = sess.router_node_name_;
  limit_ = sess.limit_;

  private_data_ = sess.private_data_;

  set_flag(flag_t::kInited, true);
  set_flag(flag_t::kRegistered, sess.check_flag(flag_t::kRegistered));

  sess.set_flag(flag_t::kReconnected, true);
  sess.set_flag(session::flag_t::kWaitReconnect, false);

  FWLOGWARNING("{} reconnect inited", *this);
  return 0;
}

int session::send_new_session() {
  if (check_flag(flag_t::kRegistered)) {
    return 0;
  }

  // send new msg
  ::atframework::gateway::server_message message;
  message.mutable_head()->set_session_id(id_);

  ::atframework::gateway::server_message_body_session *sess = message.mutable_body()->mutable_add_session();
  if (nullptr != sess) {
    sess->set_client_ip(peer_ip_);
    sess->set_client_port(peer_port_);
  }

  // send to router_
  if (0 == router_node_id_ && router_node_name_.empty()) {
    FWLOGWARNING("{} has not configure router, ignore new session notification", *this);
    return static_cast<int>(error_code_t::kInvalidRouter);
  }

  int ret = send_to_server(message);
  if (0 == ret) {
    set_flag(flag_t::kRegistered, true);
    FWLOGWARNING("{} send register notify to {}({}) success", *this, router_node_id_, router_node_name_);
  } else {
    FWLOGERROR("{} send register notify to {}({}) failed, res: {}", *this, router_node_id_, router_node_name_, ret);
  }

  return ret;
}

int session::send_remove_session() { return send_remove_session(owner_); }

int session::send_remove_session(session_manager *mgr) {
  if (!check_flag(flag_t::kRegistered)) {
    return 0;
  }

  // echo server模式不需要路由通知
  if (mgr != nullptr && mgr->get_conf().origin_conf.echo_server()) {
    FWLOGWARNING("{} ignore remove notify for echo server", *this);
    return 0;
  }

  // send remove msg
  ::atframework::gateway::server_message message;
  message.mutable_head()->set_session_id(id_);

  ::atframework::gateway::server_message_body_session *sess = message.mutable_body()->mutable_remove_session();
  if (nullptr != sess) {
    sess->set_client_ip(get_peer_host());
    sess->set_client_port(get_peer_port());
  }

  int ret = send_to_server(message, mgr);
  if (0 == ret) {
    set_flag(flag_t::kRegistered, false);
    FWLOGWARNING("{} send remove notify to {}({}) success", *this, router_node_id_, router_node_name_);
  } else {
    FWLOGERROR("{} send remove notify to {}({}) failed, res: {}", *this, router_node_id_, router_node_name_, ret);
  }

  return ret;
}

void session::on_alloc_read(size_t suggested_size, char *&out_buf, size_t &out_len) {
  if (proto_) {
    proto_->alloc_receive_buffer(suggested_size, out_buf, out_len);

    if (nullptr == out_buf && 0 == out_len) {
      close_fd(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), 0, "alloc read memory failed");
    }
  }
}

void session::on_read(int ssz, gsl::span<const unsigned char> buffer) {
  if (proto_) {
    int errcode = 0;
    proto_->read(ssz, buffer, errcode);

    if (errcode < 0) {
      FWLOGERROR("{} read data length {} failed and will be closed, res: {}", *this, buffer.size(), errcode);
      close(static_cast<int>(close_reason_t::kInvalidData), errcode, "network error");
    } else {
      FWLOGDEBUG("{} read data length {} success", *this, buffer.size());
    }
  }
}

int session::on_write_done(int status) {
  if (proto_) {
    int ret = proto_->write_done(status);

    // if about to closing and all data transferred, shutdown the socket
    if (check_flag(flag_t::kClosingFd) &&
        proto_->check_flag(atframework::gateway::libatgw_protocol_api::flag_t::kClosed)) {
      uv_shutdown(&shutdown_req_, &stream_handle_, on_evt_shutdown);
    }

    return ret;
  }

  return 0;
}

int session::close(int32_t reason, int32_t sub_reason, atfw::util::nostd::string_view message) {
  return close_with_manager(reason, sub_reason, message, owner_);
}

int session::close_with_manager(int32_t reason, int32_t sub_reason, atfw::util::nostd::string_view message,
                                session_manager *mgr) {
  // 这个接口会被多次调用（分别在关闭网络连接、重连超时、主动踢下线）
  // 重连超时的逻辑不会走后面的流程了，但是还是要通知服务器踢下线
  if (check_flag(flag_t::kRegistered) && !check_flag(flag_t::kReconnected) && !check_flag(flag_t::kWaitReconnect)) {
    send_remove_session(mgr);
  }

  if (check_flag(flag_t::kClosing)) {
    return 0;
  }

  set_flag(flag_t::kClosing, true);

  FWLOGINFO("{} close with reason: {}, {}, {}", *this, reason, sub_reason, message);
  return close_fd(reason, sub_reason, message);
}

int session::close_fd(int32_t reason, int32_t sub_reason, atfw::util::nostd::string_view message) {
  if (check_flag(flag_t::kClosingFd)) {
    return 0;
  }

  if (check_flag(flag_t::kHasFd)) {
    set_flag(flag_t::kHasFd, false);

    if (proto_) {
      proto_->close(reason, sub_reason, message);
    }

    // shutdown and close uv_stream_t
    // manager can not be used any more
    owner_ = nullptr;
    shutdown_req_.data = new ptr_t(shared_from_this());

    // if writing, wait all data written an then shutdown it
    set_flag(flag_t::kClosingFd, true);
    if (!proto_ || proto_->check_flag(atframework::gateway::libatgw_protocol_api::flag_t::kClosed)) {
      uv_shutdown(&shutdown_req_, &stream_handle_, on_evt_shutdown);
    }
    // TODO: else 设置超时强制 uv_close (uv_shutdown会等待未写出数据全部写完，超时不应该等待) , 注意多个 uv_close
    // 调用不要冲突

    FWLOGINFO("{} lost fd", *this);
  }
  FWLOGWARNING("{} close fd with reason: {}, {}, {}", *this, reason, sub_reason, message);

  return 0;
}

int session::send_to_client(gsl::span<const unsigned char> data) {
  // send to proto_
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(error_code_t::kClosing);
  }

  if (!check_flag(flag_t::kHasFd)) {
    return static_cast<int>(error_code_t::kClosing);
  }

  if (!proto_) {
    FWLOGERROR("{} lost protocol handle when send to client", *this);
    return static_cast<int>(error_code_t::kBadProtocol);
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

int session::send_to_server(::atframework::gateway::server_message &message) { return send_to_server(message, owner_); }

int session::send_to_server(::atframework::gateway::server_message &message, session_manager *mgr) {
  // echo server模式不需要路由通知
  if (mgr != nullptr && mgr->get_conf().origin_conf.echo_server()) {
    return 0;
  }

  // send to router_
  if (0 == router_node_id_ && router_node_name_.empty()) {
    FWLOGERROR("{} has not configure router", *this);
    return static_cast<int>(error_code_t::kInvalidRouter);
  }

  if (nullptr == mgr) {
    mgr = owner_;
  }

  if (nullptr == mgr) {
    FWLOGERROR("{} has lost manager and can not send ss message any more", *this);
    return static_cast<int>(error_code_t::kLostManager);
  }

  // send to server with type = ::atframework::component::service_type::EN_ATST_GATEWAY
  std::string packed_buffer;
  if (false == message.SerializeToString(&packed_buffer)) {
    FWLOGERROR("{} serialize failed and can not send server message: {}", *this, message.InitializationErrorString());
    return static_cast<int>(error_code_t::kBadData);
  }

  size_t len = packed_buffer.size();
  // recv limit
  limit_.hour_recv_bytes += len;
  limit_.minute_recv_bytes += len;
  limit_.total_recv_bytes += len;
  ++limit_.minute_recv_times;
  ++limit_.hour_recv_times;
  ++limit_.total_recv_times;

  int ret = 0;
  if (0 != router_node_id_) {
    ret = mgr->post_data(
        router_node_id_, static_cast<int32_t>(::atframework::component::service_type::kAtGateway),
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed_buffer.data()), len});
  } else {
    ret = mgr->post_data(
        router_node_name_, static_cast<int32_t>(::atframework::component::service_type::kAtGateway),
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

  uv_os_fd_t fd{};
  uv_fileno(reinterpret_cast<uv_handle_t *>(req->handle), &fd);
  FWLOGINFO("system fd {} shutdown", fd);

  uv_close(&self->raw_handle_, on_evt_closed);
}

void session::on_evt_closed(uv_handle_t *handle) {
  assert(handle && handle->data);
  if (nullptr == handle || nullptr == handle->data) {
    return;
  }

  uv_os_fd_t fd{};
  uv_fileno(handle, &fd);

  session *self = reinterpret_cast<session *>(handle->data);
  assert(self);
  self->set_flag(flag_t::kClosingFd, false);

  FWLOGINFO("{} system fd {} closed", *self, fd);

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

  if (check_flag(flag_t::kClosing)) {
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().hour_recv_bytes() > 0 &&
      limit_.hour_recv_bytes > owner_->get_conf().origin_conf.client().limit().hour_recv_bytes()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().hour_send_bytes() > 0 &&
      limit_.hour_send_bytes > owner_->get_conf().origin_conf.client().limit().hour_send_bytes()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().hour_recv_times() > 0 &&
      limit_.hour_recv_times > owner_->get_conf().origin_conf.client().limit().hour_recv_times()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().hour_send_times() > 0 &&
      limit_.hour_send_times > owner_->get_conf().origin_conf.client().limit().hour_send_times()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
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

  if (check_flag(flag_t::kClosing)) {
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().minute_recv_bytes() > 0 &&
      limit_.minute_recv_bytes > owner_->get_conf().origin_conf.client().limit().minute_recv_bytes()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().minute_recv_times() > 0 &&
      limit_.minute_recv_times > owner_->get_conf().origin_conf.client().limit().minute_recv_times()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
    return;
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().minute_send_bytes() > 0 &&
      limit_.minute_send_bytes > owner_->get_conf().origin_conf.client().limit().minute_send_bytes()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
    return;
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().minute_send_times() > 0 &&
      limit_.minute_send_times > owner_->get_conf().origin_conf.client().limit().minute_send_times()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
    return;
  }
}

void session::check_total_limit(bool check_recv, bool check_send) {
  if (nullptr == owner_) {
    return;
  }

  if (check_flag(flag_t::kClosing)) {
    return;
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().total_recv_bytes() > 0 &&
      limit_.total_recv_bytes > owner_->get_conf().origin_conf.client().limit().total_send_bytes()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }

  if (check_recv && owner_->get_conf().origin_conf.client().limit().total_recv_times() > 0 &&
      limit_.total_recv_times > owner_->get_conf().origin_conf.client().limit().total_recv_times()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().total_send_bytes() > 0 &&
      limit_.total_send_bytes > owner_->get_conf().origin_conf.client().limit().total_send_bytes()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }

  if (check_send && owner_->get_conf().origin_conf.client().limit().total_send_times() > 0 &&
      limit_.total_send_times > owner_->get_conf().origin_conf.client().limit().total_send_times()) {
    close(static_cast<int>(close_reason_t::kTraficExtended), 0, "trafic extended");
  }
}

gsl::span<const unsigned char> session::get_router_hash_data() const noexcept {
  if (proto_) {
    return proto_->get_router_hash_data();
  }

  return {};
}

}  // namespace gateway
}  // namespace atframework
