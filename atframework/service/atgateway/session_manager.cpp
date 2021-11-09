// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include <uv.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <new>
#include <sstream>

#include "config/atframe_service_types.h"

#include "session_manager.h"

namespace atframe {
namespace gateway {
namespace detail {
template <typename T>
static void session_manager_delete_stream_fn(uv_stream_t *handle) {
  if (nullptr == handle) {
    return;
  }

  T *real_conn = reinterpret_cast<T *>(handle);
  // must be closed
  assert(uv_is_closing(reinterpret_cast<uv_handle_t *>(handle)));
  delete real_conn;
}

template <typename T>
static T *session_manager_make_stream_ptr(std::shared_ptr<uv_stream_t> &res) {
  T *real_conn = new (std::nothrow) T();
  if (nullptr == real_conn) {
    return real_conn;
  }

  uv_stream_t *stream_conn = reinterpret_cast<uv_stream_t *>(real_conn);
  res = std::shared_ptr<uv_stream_t>(stream_conn, session_manager_delete_stream_fn<T>);
  stream_conn->data = nullptr;
  return real_conn;
}
}  // namespace detail

session_manager::session_manager() : evloop_(nullptr), app_node_(nullptr), last_tick_time_(0), private_data_(nullptr) {}

session_manager::~session_manager() { reset(); }

int session_manager::init(::atbus::node *bus_node, create_proto_fn_t fn) {
  evloop_ = bus_node->get_evloop();
  app_node_ = bus_node;
  create_proto_fn_ = fn;
  if (!fn) {
    WLOGERROR("create protocol function is required");
    return -1;
  }
  return 0;
}

int session_manager::listen_all() {
  int ret = 0;
  for (auto &listen_address : conf_.origin_conf.listen().address()) {
    int res = listen(listen_address.c_str());
    if (0 != res) {
      FWLOGERROR("try to listen {} failed, res: {}", listen_address, res);
    } else {
      FWLOGDEBUG("listen to {} success", listen_address);
      ++ret;
    }
  }

  return ret;
}

int session_manager::listen(const char *address) {
  // make_address
  ::atbus::channel::channel_address_t addr;
  ::atbus::channel::make_address(address, addr);
  int ret = 0;

  listen_handle_ptr_t res;
  do {
    // libuv listen and setup callbacks
    int libuv_res;
    if (0 == UTIL_STRFUNC_STRNCASE_CMP("ipv4", addr.scheme.c_str(), 4) ||
        0 == UTIL_STRFUNC_STRNCASE_CMP("ipv6", addr.scheme.c_str(), 4)) {
      uv_tcp_t *tcp_handle = ::atframe::gateway::detail::session_manager_make_stream_ptr<uv_tcp_t>(res);
      if (res) {
        uv_stream_set_blocking(res.get(), 0);
        uv_tcp_nodelay(tcp_handle, 1);
      } else {
        FWLOGERROR("create uv_tcp_t failed.");
        ret = error_code_t::EN_ECT_NETWORK;
        break;
      }

      libuv_res = uv_tcp_init(evloop_, tcp_handle);
      if (0 != libuv_res) {
        FWLOGERROR("init listen to {} failed, libuv_res: {}({})", address, libuv_res, uv_strerror(libuv_res));
        ret = error_code_t::EN_ECT_NETWORK;
        break;
      }

      if ('4' == addr.scheme[3]) {
        sockaddr_in sock_addr;
        uv_ip4_addr(addr.host.c_str(), addr.port, &sock_addr);
        libuv_res = uv_tcp_bind(tcp_handle, reinterpret_cast<const sockaddr *>(&sock_addr), 0);
        if (0 != libuv_res) {
          FWLOGERROR("bind sock to tcp/ip v4 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = error_code_t::EN_ECT_NETWORK;
          break;
        }

        libuv_res = uv_listen(res.get(), conf_.origin_conf.listen().backlog(), on_evt_accept_tcp);
        if (0 != libuv_res) {
          FWLOGERROR("listen to tcp/ip v4 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = error_code_t::EN_ECT_NETWORK;
          break;
        }

        tcp_handle->data = this;
      } else {
        sockaddr_in6 sock_addr;
        uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr);
        libuv_res = uv_tcp_bind(tcp_handle, reinterpret_cast<const sockaddr *>(&sock_addr), 0);
        if (0 != libuv_res) {
          FWLOGERROR("bind sock to tcp/ip v6 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = error_code_t::EN_ECT_NETWORK;
          break;
        }

        libuv_res = uv_listen(res.get(), conf_.origin_conf.listen().backlog(), on_evt_accept_tcp);
        if (0 != libuv_res) {
          FWLOGERROR("listen to tcp/ip v6 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = error_code_t::EN_ECT_NETWORK;
          break;
        }

        tcp_handle->data = this;
      }

    } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("unix", addr.scheme.c_str(), 4)) {
      uv_pipe_t *pipe_handle = ::atframe::gateway::detail::session_manager_make_stream_ptr<uv_pipe_t>(res);
      if (res) {
        uv_stream_set_blocking(res.get(), 0);
      } else {
        FWLOGERROR("create uv_pipe_t failed.");
        ret = error_code_t::EN_ECT_NETWORK;
        break;
      }

      libuv_res = uv_pipe_init(evloop_, pipe_handle, 1);
      if (0 != libuv_res) {
        FWLOGERROR("init listen to unix sock {} failed, libuv_res: {}({})", addr.host, libuv_res,
                   uv_strerror(libuv_res));
        ret = error_code_t::EN_ECT_NETWORK;
        break;
      }

      libuv_res = uv_pipe_bind(pipe_handle, addr.host.c_str());
      if (0 != libuv_res) {
        FWLOGERROR("bind pipe to unix sock {} failed, libuv_res: {}({})", addr.host, libuv_res, uv_strerror(libuv_res));
        ret = error_code_t::EN_ECT_NETWORK;
        break;
      }

      libuv_res = uv_listen(res.get(), conf_.origin_conf.listen().backlog(), on_evt_accept_pipe);
      if (0 != libuv_res) {
        FWLOGERROR("listen to unix sock {} failed, libuv_res: {}({})", addr.host, libuv_res, uv_strerror(libuv_res));
        ret = error_code_t::EN_ECT_NETWORK;
        break;
      }

      pipe_handle->data = this;
    } else {
      ret = error_code_t::EN_ECT_INVALID_ADDRESS;
    }
  } while (false);

  if (res) {
    if (0 == ret) {
      listen_handles_.push_back(res);
    } else {
      // ref count + 1
      res->data = new listen_handle_ptr_t(res);
      uv_close(reinterpret_cast<uv_handle_t *>(res.get()), on_evt_listen_closed);
    }
  }

  return ret;
}

int session_manager::reset() {
  // close all sessions
  for (session_map_t::iterator iter = actived_sessions_.begin(); iter != actived_sessions_.end(); ++iter) {
    if (iter->second) {
      iter->second->close(close_reason_t::EN_CRT_SERVER_CLOSED);
    }
  }
  actived_sessions_.clear();

  for (std::list<session_timeout_t>::iterator iter = first_idle_.begin(); iter != first_idle_.end(); ++iter) {
    if (iter->s) {
      iter->s->close(close_reason_t::EN_CRT_SERVER_CLOSED);
    }
  }
  first_idle_.clear();

  for (session_map_t::iterator iter = reconnect_cache_.begin(); iter != reconnect_cache_.end(); ++iter) {
    if (iter->second) {
      iter->second->close(close_reason_t::EN_CRT_SERVER_CLOSED);
    }
  }
  reconnect_cache_.clear();

  for (std::list<session_timeout_t>::iterator iter = reconnect_timeout_.begin(); iter != reconnect_timeout_.end();
       ++iter) {
    if (iter->s) {
      iter->s->close(close_reason_t::EN_CRT_SERVER_CLOSED);
    }
  }
  reconnect_timeout_.clear();

  // close all listen socks
  for (std::list<listen_handle_ptr_t>::iterator iter = listen_handles_.begin(); iter != listen_handles_.end(); ++iter) {
    if (*iter) {
      // ref count + 1
      (*iter)->data = new listen_handle_ptr_t(*iter);
      uv_close(reinterpret_cast<uv_handle_t *>((*iter).get()), on_evt_listen_closed);
    }
  }
  listen_handles_.clear();
  return 0;
}

int session_manager::tick() {
  time_t now = util::time::time_utility::get_now();
  // 每秒只需要判定一次
  if (last_tick_time_ == now) {
    return 0;
  }

  // 每分钟打印一次统计数据
  if (last_tick_time_ / util::time::time_utility::MINITE_SECONDS != now / util::time::time_utility::MINITE_SECONDS) {
#if defined(__cpluscplus) && __cpluscplus >= 201103L
    // std::list 在C++11以前可能是O(n)复杂度
    WLOGINFO(
        "[STAT] session manager: actived session %llu, reconnect session %llu, idle timer count %llu, reconnect timer "
        "count %llu",
        static_cast<unsigned long long>(actived_sessions_.size()),
        static_cast<unsigned long long>(reconnect_cache_.size()), static_cast<unsigned long long>(first_idle_.size()),
        static_cast<unsigned long long>(reconnect_timeout_.size()));
#else
    WLOGINFO("[STAT] session manager: actived session %llu, reconnect session %llu",
             static_cast<unsigned long long>(actived_sessions_.size()),
             static_cast<unsigned long long>(reconnect_cache_.size()));
#endif
  }
  last_tick_time_ = now;

  // reconnect timeout
  while (!reconnect_timeout_.empty()) {
    if (reconnect_timeout_.front().timeout > now) {
      break;
    }

    if (reconnect_timeout_.front().s) {
      session::ptr_t s = reconnect_timeout_.front().s;
      if (s->check_flag(session::flag_t::EN_FT_RECONNECTED)) {
        WLOGINFO("session 0x%llx(%p) reconnected, cleanup", static_cast<unsigned long long>(s->get_id()), s.get());
      } else {
        WLOGINFO("session 0x%llx(%p) reconnect timeout, close and cleanup",
                 static_cast<unsigned long long>(s->get_id()), s.get());
      }
      reconnect_cache_.erase(s->get_id());

      // timeout and unset EN_FT_WAIT_RECONNECT to send remove notify
      s->set_flag(session::flag_t::EN_FT_WAIT_RECONNECT, false);
      s->close_with_manager(close_reason_t::EN_CRT_LOGOUT, this);
    }
    reconnect_timeout_.pop_front();
  }

  // first idle timeout
  while (!first_idle_.empty()) {
    if (first_idle_.front().timeout > now) {
      break;
    }

    if (first_idle_.front().s) {
      session::ptr_t s = first_idle_.front().s;

      if (!s->check_flag(session::flag_t::EN_FT_REGISTERED) && !s->check_flag(session::flag_t::EN_FT_CLOSING)) {
        WLOGINFO("session 0x%llx(%p) register timeout", static_cast<unsigned long long>(s->get_id()), s.get());
        s->close(close_reason_t::EN_CRT_FIRST_IDLE);
      }
    }
    first_idle_.pop_front();
  }

  return 0;
}

int session_manager::close(session::id_t sess_id, int reason, bool allow_reconnect) {
  session_map_t::iterator iter = actived_sessions_.find(sess_id);
  if (actived_sessions_.end() == iter) {
    // if not allow reconnect, close reconnect cache
    if (!allow_reconnect) {
      iter = reconnect_cache_.find(sess_id);
      if (reconnect_cache_.end() != iter) {
        iter->second->close(reason);
        iter->second->set_flag(session::flag_t::EN_FT_WAIT_RECONNECT, false);
        reconnect_cache_.erase(iter);
      } else {
        return error_code_t::EN_ECT_SESSION_NOT_FOUND;
      }
    } else {
      return error_code_t::EN_ECT_SESSION_NOT_FOUND;
    }

    return 0;
  }

  if (conf_.origin_conf.client().reconnect_timeout().seconds() > 0 && allow_reconnect) {
    reconnect_timeout_.push_back(session_timeout_t());
    session_timeout_t &sess_timer = reconnect_timeout_.back();
    sess_timer.s = iter->second;
    sess_timer.timeout = util::time::time_utility::get_now() + conf_.origin_conf.client().reconnect_timeout().seconds();

    reconnect_cache_[sess_timer.s->get_id()] = sess_timer.s;
    FWLOGINFO("session {:#x}({}) closed and setup reconnect timeout {}(+{})", sess_timer.s->get_id(),
              reinterpret_cast<const void *>(sess_timer.s.get()), sess_timer.timeout,
              conf_.origin_conf.client().reconnect_timeout().seconds());

    // maybe transfer reconnecting session, old session still keep EN_FT_WAIT_RECONNECT flag
    sess_timer.s->set_flag(session::flag_t::EN_FT_WAIT_RECONNECT, true);

    // just close fd
    sess_timer.s->close_fd(reason);
  } else {
    FWLOGINFO("session {:#x}({}) closed and disable reconnect", iter->second->get_id(),
              reinterpret_cast<const void *>(iter->second.get()));
    iter->second->close(reason);
  }

  // erase from activited map
  actived_sessions_.erase(iter);
  return 0;
}

int session_manager::post_data(::atbus::node::bus_id_t tid, ::atframe::gw::ss_msg &msg) {
  return post_data(tid, ::atframe::component::service_type::EN_ATST_GATEWAY, msg);
}

int session_manager::post_data(::atbus::node::bus_id_t tid, int type, ::atframe::gw::ss_msg &msg) {
  // send to server with type = ::atframe::component::service_type::EN_ATST_GATEWAY
  std::string packed_buffer;
  if (false == msg.SerializeToString(&packed_buffer)) {
    FWLOGERROR("can not send ss message to {:#x} with serialize failed: {}", tid, msg.InitializationErrorString());
    return error_code_t::EN_ECT_BAD_DATA;
  }

  return post_data(tid, type, packed_buffer.data(), packed_buffer.size());
}

int session_manager::post_data(::atbus::node::bus_id_t tid, int type, const void *buffer, size_t s) {
  // send to process
  if (!app_node_) {
    return error_code_t::EN_ECT_HANDLE_NOT_FOUND;
  }

  return app_node_->send_data(tid, type, buffer, s);
}

int session_manager::push_data(session::id_t sess_id, const void *buffer, size_t s) {
  session_map_t::iterator iter = actived_sessions_.find(sess_id);
  if (actived_sessions_.end() == iter) {
    return error_code_t::EN_ECT_SESSION_NOT_FOUND;
  }

  return iter->second->send_to_client(buffer, s);
}

int session_manager::broadcast_data(const void *buffer, size_t s) {
  int ret = error_code_t::EN_ECT_SESSION_NOT_FOUND;
  for (session_map_t::iterator iter = actived_sessions_.begin(); iter != actived_sessions_.end(); ++iter) {
    if (iter->second->check_flag(session::flag_t::EN_FT_REGISTERED)) {
      int res = iter->second->send_to_client(buffer, s);
      if (0 != res) {
        WLOGERROR("broadcast data to session 0x%llx failed, res: %d", static_cast<unsigned long long>(iter->first),
                  res);
      }

      if (0 != ret) {
        ret = res;
      }
    }
  }

  return ret;
}

int session_manager::set_session_router(session::id_t sess_id, ::atbus::node::bus_id_t router) {
  session_map_t::iterator iter = actived_sessions_.find(sess_id);
  if (actived_sessions_.end() == iter) {
    return error_code_t::EN_ECT_SESSION_NOT_FOUND;
  }

  iter->second->set_router(router);
  return 0;
}

int session_manager::reconnect(session &new_sess, session::id_t old_sess_id) {
  // find old session
  bool has_reconnect_checked = false;
  session_map_t::iterator iter = reconnect_cache_.find(old_sess_id);
  // replace the existed session, in case of the lost connection has not be detected
  if (iter == reconnect_cache_.end()) {
    iter = actived_sessions_.find(old_sess_id);
    if (iter != actived_sessions_.end() && nullptr != new_sess.get_protocol_handle() &&
        nullptr != iter->second->get_protocol_handle()) {
      has_reconnect_checked = true;
      if (new_sess.get_protocol_handle()->check_reconnect(iter->second->get_protocol_handle())) {
        WLOGDEBUG("session %s:%d try to reconnect 0x%llx and need to close old connection %p",
                  new_sess.get_peer_host().c_str(), new_sess.get_peer_port(),
                  static_cast<unsigned long long>(old_sess_id), iter->second.get());
        close(old_sess_id, close_reason_t::EN_CRT_LOGOUT, true);
      } else {
        WLOGDEBUG("session %s:%d try to reconnect 0x%llx to old connection %p, but check_reconnect failed",
                  new_sess.get_peer_host().c_str(), new_sess.get_peer_port(),
                  static_cast<unsigned long long>(old_sess_id), iter->second.get());
      }
    } else if (iter == actived_sessions_.end()) {
      WLOGDEBUG("old session 0x%llx not found", static_cast<unsigned long long>(old_sess_id));
    } else if (nullptr == iter->second->get_protocol_handle()) {
      WLOGERROR("old session 0x%llx(%p) has no protocol handle", static_cast<unsigned long long>(old_sess_id),
                iter->second.get());
    }

    iter = reconnect_cache_.find(old_sess_id);
  }

  if (iter == reconnect_cache_.end() || !iter->second) {
    return error_code_t::EN_ECT_SESSION_NOT_FOUND;
  }

  // check if old session closed
  if (iter->second->check_flag(session::flag_t::EN_FT_HAS_FD)) {
    return error_code_t::EN_ECT_ALREADY_HAS_FD;
  }

  // check if old session not reconnected
  if (iter->second->check_flag(session::flag_t::EN_FT_RECONNECTED)) {
    WLOGERROR("session %s:%d try to reconnect 0x%llx, but old session already reconnected",
              new_sess.get_peer_host().c_str(), new_sess.get_peer_port(), static_cast<unsigned long long>(old_sess_id));
    return error_code_t::EN_ECT_SESSION_NOT_FOUND;
  }

  // run proto check
  if (nullptr == new_sess.get_protocol_handle() || nullptr == iter->second->get_protocol_handle()) {
    return error_code_t::EN_ECT_BAD_PROTOCOL;
  }

  if (!has_reconnect_checked && !new_sess.get_protocol_handle()->check_reconnect(iter->second->get_protocol_handle())) {
    return error_code_t::EN_ECT_REFUSE_RECONNECT;
  }

  // init with reconnect
  new_sess.init_reconnect(*iter->second);
  // close old session
  iter->second->close(close_reason_t::EN_CRT_LOGOUT);

  // erase reconnect cache, this session id may reconnect again
  reconnect_cache_.erase(iter);
  return 0;
}

int session_manager::active_session(session::ptr_t sess) {
  if (!sess) {
    return error_code_t::EN_ECT_SESSION_NOT_FOUND;
  }

  session_map_t::iterator iter = actived_sessions_.find(sess->get_id());
  if (iter != actived_sessions_.end()) {
    close(sess->get_id(), close_reason_t::EN_CRT_KICKOFF);
  }

  int ret = sess->send_new_session();
  if (ret < 0) {
    return ret;
  }

  actived_sessions_[sess->get_id()] = sess;
  return 0;
}

void session_manager::on_evt_accept_tcp(uv_stream_t *server, int status) {
  if (0 != status) {
    WLOGERROR("accept tcp socket failed, status: %d", status);
    return;
  }

  // server's data is session_manager
  session_manager *mgr = reinterpret_cast<session_manager *>(server->data);
  assert(mgr);
  if (nullptr == mgr) {
    WLOGERROR("session_manager not found");
    return;
  }

  session::ptr_t sess;

  {
    std::unique_ptr< ::atframe::gateway::proto_base> proto;
    if (mgr->create_proto_fn_) {
      mgr->create_proto_fn_().swap(proto);
    }

    // create proto object and session object
    if (proto) {
      sess = session::create(mgr, proto);
    }
  }

  if (!sess || nullptr == sess->get_protocol_handle()) {
    WLOGERROR("create proto fn is null or create proto object failed or create session failed");
    listen_handle_ptr_t sp;
    uv_tcp_t *sock = detail::session_manager_make_stream_ptr<uv_tcp_t>(sp);
    if (nullptr != sock) {
      uv_tcp_init(server->loop, sock);
      uv_accept(server, reinterpret_cast<uv_stream_t *>(sock));
      sock->data = new listen_handle_ptr_t(sp);
      uv_close(reinterpret_cast<uv_handle_t *>(sock), on_evt_listen_closed);
    }
    return;
  }

  // setup send buffer size
  sess->get_protocol_handle()->set_recv_buffer_limit(ATBUS_MACRO_MSG_LIMIT, 2);
  sess->get_protocol_handle()->set_send_buffer_limit(mgr->conf_.origin_conf.client().send_buffer_size(), 0);

  // setup default router
  sess->set_router(mgr->conf_.origin_conf.client().default_router());

  // create proto object and session object
  int res = sess->accept_tcp(server);
  if (0 != res) {
    sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
    return;
  }

  // check session number limit
  if (mgr->conf_.origin_conf.listen().max_client() > 0 &&
      mgr->reconnect_cache_.size() + mgr->actived_sessions_.size() >= mgr->conf_.origin_conf.listen().max_client()) {
    FWLOGWARNING("accept tcp socket failed, gateway have too many sessions now");
    sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
    return;
  }

  if (mgr->on_create_session_fn_) {
    mgr->on_create_session_fn_(sess.get(), sess->get_uv_stream());
  }

  // first idle timeout
  mgr->first_idle_.push_back(session_timeout_t());
  session_timeout_t &sess_timeout = mgr->first_idle_.back();
  sess_timeout.s = sess;
  if (mgr->conf_.origin_conf.client().first_idle_timeout().seconds() > 0) {
    sess_timeout.timeout =
        util::time::time_utility::get_now() + mgr->conf_.origin_conf.client().first_idle_timeout().seconds();
  } else {
    sess_timeout.timeout = util::time::time_utility::get_now() + 1;
  }
  WLOGINFO("accept a tcp socket(%s:%d), create sesson %p and to wait for handshake now, expired time is %lld(+%lld)",
           sess->get_peer_host().c_str(), sess->get_peer_port(), sess.get(),
           static_cast<long long>(sess_timeout.timeout),
           static_cast<long long>(sess_timeout.timeout - util::time::time_utility::get_now()));
}

void session_manager::on_evt_accept_pipe(uv_stream_t *server, int status) {
  if (0 != status) {
    WLOGERROR("accept tcp socket failed, status: %d", status);
    return;
  }

  // server's data is session_manager
  session_manager *mgr = reinterpret_cast<session_manager *>(server->data);
  assert(mgr);
  if (nullptr == mgr) {
    WLOGERROR("session_manager not found");
    return;
  }

  std::unique_ptr< ::atframe::gateway::proto_base> proto;
  if (mgr->create_proto_fn_) {
    mgr->create_proto_fn_().swap(proto);
  }

  session::ptr_t sess;
  // create proto object and session object
  if (proto) {
    sess = session::create(mgr, proto);
  }

  if (!sess) {
    WLOGERROR("create proto fn is null or create proto object failed or create session failed");
    listen_handle_ptr_t sp;
    uv_pipe_t *sock = detail::session_manager_make_stream_ptr<uv_pipe_t>(sp);
    if (nullptr != sock) {
      uv_pipe_init(server->loop, sock, 1);
      uv_accept(server, reinterpret_cast<uv_stream_t *>(sock));
      sock->data = new listen_handle_ptr_t(sp);
      uv_close(reinterpret_cast<uv_handle_t *>(sock), on_evt_listen_closed);
    }
    return;
  }

  // setup send buffer size
  proto->set_recv_buffer_limit(ATBUS_MACRO_MSG_LIMIT, 2);
  proto->set_send_buffer_limit(mgr->conf_.origin_conf.client().send_buffer_size(), 0);

  // setup default router
  sess->set_router(mgr->conf_.origin_conf.client().default_router());

  int res = sess->accept_pipe(server);
  if (0 != res) {
    sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
    return;
  }

  // check session number limit
  if (mgr->conf_.origin_conf.listen().max_client() > 0 &&
      mgr->reconnect_cache_.size() + mgr->actived_sessions_.size() >= mgr->conf_.origin_conf.listen().max_client()) {
    sess->close(close_reason_t::EN_CRT_SERVER_BUSY);
    return;
  }

  if (mgr->on_create_session_fn_) {
    mgr->on_create_session_fn_(sess.get(), sess->get_uv_stream());
  }

  // first idle timeout
  mgr->first_idle_.push_back(session_timeout_t());
  session_timeout_t &sess_timeout = mgr->first_idle_.back();
  sess_timeout.s = sess;
  if (mgr->conf_.origin_conf.client().first_idle_timeout().seconds() > 0) {
    sess_timeout.timeout =
        util::time::time_utility::get_now() + mgr->conf_.origin_conf.client().first_idle_timeout().seconds();
  } else {
    sess_timeout.timeout = util::time::time_utility::get_now() + 1;
  }
}

void session_manager::on_evt_listen_closed(uv_handle_t *handle) {
  // delete shared ptr
  listen_handle_ptr_t *ptr = reinterpret_cast<listen_handle_ptr_t *>(handle->data);
  delete ptr;
}
}  // namespace gateway
}  // namespace atframe
