// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "session_manager.h"

#include <uv.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atframe/modules/etcd_module.h>

#include <gsl/util>
#include <new>

#include "config/atframe_service_types.h"
#include "string/string_format.h"

namespace atframework {
namespace gateway {
namespace {
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
}  // namespace

session_manager::session_manager() : evloop_(nullptr), app_(nullptr), last_tick_time_(0), private_data_(nullptr) {}

session_manager::~session_manager() { reset(); }

// NOLINTNEXTLINE(performance-unnecessary-value-param)
int session_manager::init(::atfw::atapp::app *app_inst, create_proto_fn_t fn) {
  evloop_ = app_inst->get_evloop();
  app_ = app_inst;
  create_proto_fn_ = fn;
  if (!fn) {
    FWLOGERROR("{}", "create protocol function is required");
    return -1;
  }

  if (!discovery_index_) {
    discovery_index_ = component::service_discovery_index::create(app_inst->get_etcd_module());
    if (!discovery_index_) {
      FWLOGERROR("create service discovery index failed");
      return -1;
    }

    discovery_index_->initialize();
  }
  return 0;
}

int session_manager::listen_all() {
  int ret = 0;
  for (const auto &listen_address : conf_.origin_conf.listen().address()) {
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
    int libuv_res = 0;
    if (0 == UTIL_STRFUNC_STRNCASE_CMP("ipv4", addr.scheme.c_str(), 4) ||
        0 == UTIL_STRFUNC_STRNCASE_CMP("ipv6", addr.scheme.c_str(), 4)) {
      uv_tcp_t *tcp_handle = session_manager_make_stream_ptr<uv_tcp_t>(res);
      if (res) {
        uv_stream_set_blocking(res.get(), 0);
        uv_tcp_nodelay(tcp_handle, 1);
      } else {
        FWLOGERROR("create uv_tcp_t failed.");
        ret = static_cast<int>(error_code_t::kNetwork);
        break;
      }

      libuv_res = uv_tcp_init(evloop_, tcp_handle);
      if (0 != libuv_res) {
        FWLOGERROR("init listen to {} failed, libuv_res: {}({})", address, libuv_res, uv_strerror(libuv_res));
        ret = static_cast<int>(error_code_t::kNetwork);
        break;
      }

      if ('4' == addr.scheme[3]) {
        sockaddr_in sock_addr{};
        uv_ip4_addr(addr.host.c_str(), addr.port, &sock_addr);
        libuv_res = uv_tcp_bind(tcp_handle, reinterpret_cast<const sockaddr *>(&sock_addr), 0);
        if (0 != libuv_res) {
          FWLOGERROR("bind sock to tcp/ip v4 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = static_cast<int>(error_code_t::kNetwork);
          break;
        }

        libuv_res = uv_listen(res.get(), conf_.origin_conf.listen().backlog(), on_evt_accept_tcp);
        if (0 != libuv_res) {
          FWLOGERROR("listen to tcp/ip v4 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = static_cast<int>(error_code_t::kNetwork);
          break;
        }

        tcp_handle->data = this;
      } else {
        sockaddr_in6 sock_addr{};
        uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr);
        libuv_res = uv_tcp_bind(tcp_handle, reinterpret_cast<const sockaddr *>(&sock_addr), 0);
        if (0 != libuv_res) {
          FWLOGERROR("bind sock to tcp/ip v6 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = static_cast<int>(error_code_t::kNetwork);
          break;
        }

        libuv_res = uv_listen(res.get(), conf_.origin_conf.listen().backlog(), on_evt_accept_tcp);
        if (0 != libuv_res) {
          FWLOGERROR("listen to tcp/ip v6 {}:{} failed, libuv_res: {}({})", addr.host, addr.port, libuv_res,
                     uv_strerror(libuv_res));
          ret = static_cast<int>(error_code_t::kNetwork);
          break;
        }

        tcp_handle->data = this;
      }

    } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("unix", addr.scheme.c_str(), 4)) {
      uv_pipe_t *pipe_handle = session_manager_make_stream_ptr<uv_pipe_t>(res);
      if (res) {
        uv_stream_set_blocking(res.get(), 0);
      } else {
        FWLOGERROR("create uv_pipe_t failed.");
        ret = static_cast<int>(error_code_t::kNetwork);
        break;
      }

      libuv_res = uv_pipe_init(evloop_, pipe_handle, 1);
      if (0 != libuv_res) {
        FWLOGERROR("init listen to unix sock {} failed, libuv_res: {}({})", addr.host, libuv_res,
                   uv_strerror(libuv_res));
        ret = static_cast<int>(error_code_t::kNetwork);
        break;
      }

      libuv_res = uv_pipe_bind(pipe_handle, addr.host.c_str());
      if (0 != libuv_res) {
        FWLOGERROR("bind pipe to unix sock {} failed, libuv_res: {}({})", addr.host, libuv_res, uv_strerror(libuv_res));
        ret = static_cast<int>(error_code_t::kNetwork);
        break;
      }

      libuv_res = uv_listen(res.get(), conf_.origin_conf.listen().backlog(), on_evt_accept_pipe);
      if (0 != libuv_res) {
        FWLOGERROR("listen to unix sock {} failed, libuv_res: {}({})", addr.host, libuv_res, uv_strerror(libuv_res));
        ret = static_cast<int>(error_code_t::kNetwork);
        break;
      }

      pipe_handle->data = this;
    } else {
      ret = static_cast<int>(error_code_t::kInvalidAddress);
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
      iter->second->close(static_cast<int>(close_reason_t::kServerClosed), 0, "server closed");
    }
  }
  actived_sessions_.clear();

  for (std::list<session_timeout_t>::iterator iter = first_idle_.begin(); iter != first_idle_.end(); ++iter) {
    if (iter->s) {
      iter->s->close(static_cast<int>(close_reason_t::kServerClosed), 0, "server closed");
    }
  }
  first_idle_.clear();

  for (session_map_t::iterator iter = reconnect_cache_.begin(); iter != reconnect_cache_.end(); ++iter) {
    if (iter->second) {
      iter->second->close(static_cast<int>(close_reason_t::kServerClosed), 0, "server closed");
    }
  }
  reconnect_cache_.clear();

  for (std::list<session_timeout_t>::iterator iter = reconnect_timeout_.begin(); iter != reconnect_timeout_.end();
       ++iter) {
    if (iter->s) {
      iter->s->close(static_cast<int>(close_reason_t::kServerClosed), 0, "server closed");
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
  time_t now = atfw::util::time::time_utility::get_now();
  // 每秒只需要判定一次
  if (last_tick_time_ == now) {
    return 0;
  }

  // 每分钟打印一次统计数据
  if (last_tick_time_ / atfw::util::time::time_utility::MINITE_SECONDS !=
      now / atfw::util::time::time_utility::MINITE_SECONDS) {
    // std::list 在C++11以前可能是O(n)复杂度
    FWLOGINFO(
        "[STAT] session manager: actived session {}, reconnect session {}, idle timer count {}, reconnect timer "
        "count {}",
        actived_sessions_.size(), reconnect_cache_.size(), first_idle_.size(), reconnect_timeout_.size());
  }
  last_tick_time_ = now;

  // reconnect timeout
  while (!reconnect_timeout_.empty()) {
    if (reconnect_timeout_.front().timeout > now) {
      break;
    }

    if (reconnect_timeout_.front().s) {
      session::ptr_t s = reconnect_timeout_.front().s;
      if (s->check_flag(session::flag_t::kReconnected)) {
        FWLOGINFO("{} reconnected, cleanup", *s);
      } else {
        FWLOGINFO("{} reconnect timeout, close and cleanup", *s);
      }
      reconnect_cache_.erase(s->get_id());

      // timeout and unset kWaitReconnect to send remove notify
      s->set_flag(session::flag_t::kWaitReconnect, false);
      s->close_with_manager(static_cast<int>(close_reason_t::kLogout), 0, "logout", this);
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

      if (!s->check_flag(session::flag_t::kRegistered) && !s->check_flag(session::flag_t::kClosing)) {
        FWLOGINFO("{} register timeout", *s);
        s->close(static_cast<int>(close_reason_t::kFirstIdle), 0, "idle timeout");
      }
    }
    first_idle_.pop_front();
  }

  return 0;
}

int session_manager::close(session::id_t sess_id, int32_t reason, int32_t sub_reason,
                           atfw::util::nostd::string_view message, bool allow_reconnect) {
  session_map_t::mapped_type session_ptr;

  // not found
  do {
    session_map_t::iterator iter = actived_sessions_.find(sess_id);
    if (actived_sessions_.end() != iter) {
      session_ptr = iter->second;
      actived_sessions_.erase(iter);
      break;
    }

    if (allow_reconnect) {
      return static_cast<int>(error_code_t::kSessionNotFound);
    }

    iter = reconnect_cache_.find(sess_id);
    if (reconnect_cache_.end() == iter) {
      return static_cast<int>(error_code_t::kSessionNotFound);
    }
    session_ptr = iter->second;
    reconnect_cache_.erase(iter);
    // 重入时直接忽略
    if (!session_ptr) {
    }

    if (session_ptr->check_flag(session::flag_t::kManagerClosing)) {
      return 0;
    }

    session_ptr->close(reason, sub_reason, message);
    session_ptr->set_flag(session::flag_t::kWaitReconnect, false);
    return 0;
  } while (false);

  // 防止重入
  if (session_ptr) {
    if (session_ptr->check_flag(session::flag_t::kManagerClosing)) {
      return 0;
    }
    session_ptr->set_flag(session::flag_t::kManagerClosing, true);
  }
  auto flag_guard = gsl::finally([session_ptr] {
    if (session_ptr) {
      session_ptr->set_flag(session::flag_t::kManagerClosing, false);
    }
  });

  if (conf_.origin_conf.client().reconnect_timeout().seconds() > 0 && allow_reconnect) {
    reconnect_timeout_.emplace_back();
    session_timeout_t &sess_timer = reconnect_timeout_.back();
    sess_timer.s = session_ptr;
    sess_timer.timeout =
        atfw::util::time::time_utility::get_now() + conf_.origin_conf.client().reconnect_timeout().seconds();

    reconnect_cache_[sess_timer.s->get_id()] = sess_timer.s;
    FWLOGINFO("{} closed and setup reconnect timeout {}(+{})", *sess_timer.s, sess_timer.timeout,
              conf_.origin_conf.client().reconnect_timeout().seconds());

    // maybe transfer reconnecting session, old session still keeps kWaitReconnect flag
    sess_timer.s->set_flag(session::flag_t::kWaitReconnect, true);

    // just close fd
    sess_timer.s->close_fd(reason, sub_reason, message);
  } else if (session_ptr) {
    FWLOGINFO("{} closed and disable reconnect", *session_ptr);
    session_ptr->close(reason, sub_reason, message);
  }

  return 0;
}

void session_manager::reload() {
  ++conf_.version;

  if (discovery_index_) {
    discovery_index_->reload();
  }
}

void session_manager::cleanup() {
  if (discovery_index_) {
    discovery_index_->cleanup();
  }

  app_ = nullptr;
}

int session_manager::post_data(::atbus::bus_id_t tid, ::atframework::gateway::server_message &message) {
  return post_data(tid, static_cast<int32_t>(::atframework::component::service_type::kAtGateway), message);
}

int session_manager::post_data(::atbus::bus_id_t tid, int32_t type, ::atframework::gateway::server_message &message) {
  // send to server with type = ::atframework::component::service_type::EN_ATST_GATEWAY
  std::string packed_buffer;
  if (false == message.SerializeToString(&packed_buffer)) {
    FWLOGERROR("can not send server message to {:#x} with serialize failed: {}", tid,
               message.InitializationErrorString());
    return static_cast<int>(error_code_t::kBadData);
  }

  return post_data(tid, type,
                   gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed_buffer.data()),
                                                  packed_buffer.size()});
}

int session_manager::post_data(::atbus::bus_id_t tid, int32_t type, gsl::span<const unsigned char> data) {
  // send to process
  if (nullptr == app_) {
    return static_cast<int>(error_code_t::kLostManager);
  }

  // echo server模式不用发给下游
  if (get_conf().origin_conf.echo_server()) {
    return 0;
  }

  return app_->send_message(tid, type, data);
}

int session_manager::post_data(const std::string &tname, ::atframework::gateway::server_message &message) {
  return post_data(tname, static_cast<int32_t>(::atframework::component::service_type::kAtGateway), message);
}

int session_manager::post_data(const std::string &tname, int32_t type,
                               ::atframework::gateway::server_message &message) {
  // send to server with type = ::atframework::component::service_type::EN_ATST_GATEWAY
  std::string packed_buffer;
  if (false == message.SerializeToString(&packed_buffer)) {
    FWLOGERROR("can not send server message to {} with serialize failed: {}", tname,
               message.InitializationErrorString());
    return static_cast<int>(error_code_t::kBadData);
  }

  return post_data(tname, type,
                   gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed_buffer.data()),
                                                  packed_buffer.size()});
}

int session_manager::post_data(const std::string &tname, int32_t type, gsl::span<const unsigned char> data) {
  // send to process
  if (nullptr == app_) {
    return static_cast<int>(error_code_t::kLostManager);
  }

  // echo server模式不用发给下游
  if (get_conf().origin_conf.echo_server()) {
    return 0;
  }

  return app_->send_message(tname, type, data);
}

int session_manager::push_data(session::id_t sess_id, gsl::span<const unsigned char> data) {
  session_map_t::iterator iter = actived_sessions_.find(sess_id);
  if (actived_sessions_.end() == iter) {
    return static_cast<int>(error_code_t::kSessionNotFound);
  }

  return iter->second->send_to_client(data);
}

int session_manager::broadcast_data(gsl::span<const unsigned char> data) {
  int ret = static_cast<int>(error_code_t::kSessionNotFound);
  for (session_map_t::iterator iter = actived_sessions_.begin(); iter != actived_sessions_.end(); ++iter) {
    if (iter->second->check_flag(session::flag_t::kRegistered)) {
      int res = iter->second->send_to_client(data);
      if (0 != res) {
        FWLOGERROR("broadcast data to session {} failed, res: {}", iter->first, res);
      }

      if (0 != ret) {
        ret = res;
      }
    }
  }

  return ret;
}

int session_manager::set_session_router(session::id_t sess_id, ::atbus::bus_id_t router_node_id,
                                        const std::string &router_node_name) {
  session_map_t::iterator iter = actived_sessions_.find(sess_id);
  if (actived_sessions_.end() == iter) {
    return static_cast<int>(error_code_t::kSessionNotFound);
  }

  if (router_node_id == iter->second->get_router_id() && router_node_name == iter->second->get_router_name()) {
    return 0;
  }

  iter->second->send_remove_session();
  iter->second->set_router(router_node_id, router_node_name);
  iter->second->send_new_session();
  FWLOGINFO("{} set router to {}:{}", *iter->second, router_node_id, router_node_name);
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
        FWLOGDEBUG("{} try to reconnect {} and need to close old connection {}", new_sess, old_sess_id, *iter->second);
        close(old_sess_id, static_cast<int>(close_reason_t::kLogout), 0, "logout", true);
      } else {
        FWLOGDEBUG("{} try to reconnect {} to old connection {}, but check_reconnect failed", new_sess, old_sess_id,
                   *iter->second);
      }
    } else if (iter == actived_sessions_.end()) {
      FWLOGDEBUG("old session {} not found", old_sess_id);
    } else if (nullptr == iter->second->get_protocol_handle()) {
      FWLOGERROR("old session 0x{}({}) has no protocol handle", old_sess_id,
                 reinterpret_cast<const void *>(iter->second.get()));
    }

    iter = reconnect_cache_.find(old_sess_id);
  }

  if (iter == reconnect_cache_.end() || !iter->second) {
    return static_cast<int>(error_code_t::kSessionNotFound);
  }

  // check if old session closed
  if (iter->second->check_flag(session::flag_t::kHasFd)) {
    return static_cast<int>(error_code_t::kAlreadyHasFd);
  }

  // check if old session not reconnected
  if (iter->second->check_flag(session::flag_t::kReconnected)) {
    FWLOGERROR("{} try to reconnect {}, but old session already reconnected", new_sess, old_sess_id);
    return static_cast<int>(error_code_t::kSessionNotFound);
  }

  // run proto check
  if (nullptr == new_sess.get_protocol_handle() || nullptr == iter->second->get_protocol_handle()) {
    return static_cast<int>(error_code_t::kBadProtocol);
  }

  if (!has_reconnect_checked && !new_sess.get_protocol_handle()->check_reconnect(iter->second->get_protocol_handle())) {
    return static_cast<int>(error_code_t::kRefuseReconnect);
  }

  // init with reconnect
  new_sess.init_reconnect(*iter->second);
  // close old session
  iter->second->close(static_cast<int>(close_reason_t::kLogout), 0, "logout");

  // erase reconnect cache, this session id may reconnect again
  reconnect_cache_.erase(iter);
  return 0;
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
int session_manager::active_session(session::ptr_t sess) {
  if (!sess) {
    return static_cast<int>(error_code_t::kSessionNotFound);
  }

  session_map_t::iterator iter = actived_sessions_.find(sess->get_id());
  if (iter != actived_sessions_.end()) {
    close(sess->get_id(), static_cast<int>(close_reason_t::kKickoff), 0, "kickoff");
  }

  // echo server 模式不需要路由通知
  if (!get_conf().origin_conf.echo_server()) {
    int ret = sess->send_new_session();
    if (ret < 0) {
      return ret;
    }
  }

  actived_sessions_[sess->get_id()] = sess;

  // setup default router
  assign_default_router(*sess);
  return 0;
}

void session_manager::assign_default_router(session &sess) const {
  bool by_setting = true;
  const atfw::atapp::protocol::atapp_metadata *policy_selector = nullptr;
  if (conf_.origin_conf.client().default_router().has_policy_selector()) {
    policy_selector = &conf_.origin_conf.client().default_router().policy_selector();
  }

  atfw::atapp::etcd_discovery_set::ptr_t select_set;
  if (discovery_index_) {
    if (conf_.origin_conf.client().default_router().type_id() != 0) {
      select_set = discovery_index_->get_discovery_index_by_type(conf_.origin_conf.client().default_router().type_id());
    } else if (!conf_.origin_conf.client().default_router().type_name().empty()) {
      select_set =
          discovery_index_->get_discovery_index_by_type(conf_.origin_conf.client().default_router().type_name());
    }
  }

  switch (conf_.origin_conf.client().default_router().policy()) {
    case ::atframework::gateway::atgateway_router_policy::EN_ATGW_ROUTER_POLICY_RANDOM: {
      if (!select_set) {
        break;
      }
      auto select_node = select_set->get_node_by_random(policy_selector);
      if (select_node) {
        sess.set_router(select_node->get_discovery_info().id(), select_node->get_discovery_info().name());
        by_setting = false;
      }
      break;
    }
    case ::atframework::gateway::atgateway_router_policy::EN_ATGW_ROUTER_POLICY_ROUND_ROBIN: {
      if (!select_set) {
        break;
      }
      auto select_node = select_set->get_node_by_round_robin(policy_selector);
      if (select_node) {
        sess.set_router(select_node->get_discovery_info().id(), select_node->get_discovery_info().name());
        by_setting = false;
      }
      break;
    }
    case ::atframework::gateway::atgateway_router_policy::EN_ATGW_ROUTER_POLICY_HASH: {
      if (!select_set) {
        break;
      }
      atfw::atapp::etcd_discovery_set::node_hash_type select_node;
      auto hash_data_span = sess.get_router_hash_data();
      if (!hash_data_span.empty()) {
        select_node = select_set->get_node_hash_by_consistent_hash(hash_data_span, policy_selector);
      } else {
        std::string address = atfw::util::string::format("{}:{}", sess.get_peer_host(), sess.get_peer_port());
        select_node = select_set->get_node_hash_by_consistent_hash(address, policy_selector);
      }
      if (select_node.node) {
        sess.set_router(select_node.node->get_discovery_info().id(), select_node.node->get_discovery_info().name());
        by_setting = false;
      }
      break;
    }
    default:
      break;
  }

  if (by_setting && discovery_index_) {
    if (conf_.origin_conf.client().default_router().node_id() != 0) {
      auto find_node = discovery_index_->get_discovery_by_id(conf_.origin_conf.client().default_router().node_id());
      if (find_node) {
        sess.set_router(find_node->get_discovery_info().id(), find_node->get_discovery_info().name());
        by_setting = false;
      }
    }

    if (by_setting && !conf_.origin_conf.client().default_router().node_name().empty()) {
      auto find_node = discovery_index_->get_discovery_by_name(conf_.origin_conf.client().default_router().node_name());
      if (find_node) {
        sess.set_router(find_node->get_discovery_info().id(), find_node->get_discovery_info().name());
        by_setting = false;
      }
    }
  }

  if (sess.get_router_id() == 0 && sess.get_router_name().empty()) {
    FWLOGWARNING("{} has no default router assigned", sess);
  } else {
    FWLOGINFO("{} set default router to {}:{}", sess, sess.get_router_id(), sess.get_router_name());
  }
}

void session_manager::on_evt_accept_tcp(uv_stream_t *server, int status) {
  if (0 != status) {
    FWLOGERROR("accept tcp socket failed, status: {}", status);
    return;
  }

  // server's data is session_manager
  session_manager *mgr = reinterpret_cast<session_manager *>(server->data);
  assert(mgr);
  if (nullptr == mgr) {
    FWLOGERROR("{}", "session_manager not found");
    return;
  }

  session::ptr_t sess;

  {
    std::unique_ptr< ::atframework::gateway::libatgw_protocol_api> proto;
    if (mgr->create_proto_fn_) {
      mgr->create_proto_fn_().swap(proto);
    }

    // create proto object and session object
    if (proto) {
      sess = session::create(mgr, proto);
    }
  }

  if (!sess || nullptr == sess->get_protocol_handle()) {
    FWLOGERROR("{}", "create proto fn is null or create proto object failed or create session failed");
    listen_handle_ptr_t sp;
    uv_tcp_t *sock = session_manager_make_stream_ptr<uv_tcp_t>(sp);
    if (nullptr != sock) {
      uv_tcp_init(server->loop, sock);
      uv_accept(server, reinterpret_cast<uv_stream_t *>(sock));
      sock->data = new listen_handle_ptr_t(sp);
      uv_close(reinterpret_cast<uv_handle_t *>(sock), on_evt_listen_closed);
    }
    return;
  }

  // setup send buffer size
  sess->get_protocol_handle()->set_receive_buffer_limit(mgr->conf_.origin_conf.client().recv_buffer_size(), 0);
  sess->get_protocol_handle()->set_send_buffer_limit(mgr->conf_.origin_conf.client().send_buffer_size(), 0);

  // create proto object and session object
  int res = sess->accept_tcp(server);
  if (0 != res) {
    sess->close(static_cast<int>(close_reason_t::kServerBusy), 0, "server busy");
    return;
  }

  // check session number limit
  if (mgr->conf_.origin_conf.listen().max_client() > 0 &&
      mgr->reconnect_cache_.size() + mgr->actived_sessions_.size() >= mgr->conf_.origin_conf.listen().max_client()) {
    FWLOGWARNING("accept tcp socket failed, gateway have too many sessions now");
    sess->close(static_cast<int>(close_reason_t::kServerBusy), 0, "server busy");
    return;
  }

  if (mgr->on_create_session_fn_) {
    mgr->on_create_session_fn_(sess.get(), sess->get_uv_stream());
  }

  // first idle timeout
  mgr->first_idle_.emplace_back();
  session_timeout_t &sess_timeout = mgr->first_idle_.back();
  sess_timeout.s = sess;
  if (mgr->conf_.origin_conf.client().first_idle_timeout().seconds() > 0) {
    sess_timeout.timeout =
        atfw::util::time::time_utility::get_now() + mgr->conf_.origin_conf.client().first_idle_timeout().seconds();
  } else {
    sess_timeout.timeout = atfw::util::time::time_utility::get_now() + 1;
  }
  FWLOGINFO("accept a tcp socket({}:{}), create sesson {} and to wait for handshake now, expired time is {}(+{})",
            sess->get_peer_host(), sess->get_peer_port(), reinterpret_cast<const void *>(sess.get()),
            sess_timeout.timeout, sess_timeout.timeout - atfw::util::time::time_utility::get_now());
}

void session_manager::on_evt_accept_pipe(uv_stream_t *server, int status) {
  if (0 != status) {
    FWLOGERROR("accept pipe socket failed, status: {}", status);
    return;
  }

  // server's data is session_manager
  session_manager *mgr = reinterpret_cast<session_manager *>(server->data);
  assert(mgr);
  if (nullptr == mgr) {
    FWLOGERROR("{}", "session_manager not found");
    return;
  }

  std::unique_ptr< ::atframework::gateway::libatgw_protocol_api> proto;
  if (mgr->create_proto_fn_) {
    mgr->create_proto_fn_().swap(proto);
  }

  session::ptr_t sess;
  // create proto object and session object
  if (proto) {
    sess = session::create(mgr, proto);
  }

  if (!sess) {
    FWLOGERROR("{}", "create proto fn is null or create proto object failed or create session failed");
    listen_handle_ptr_t sp;
    uv_pipe_t *sock = session_manager_make_stream_ptr<uv_pipe_t>(sp);
    if (nullptr != sock) {
      uv_pipe_init(server->loop, sock, 1);
      uv_accept(server, reinterpret_cast<uv_stream_t *>(sock));
      sock->data = new listen_handle_ptr_t(sp);
      uv_close(reinterpret_cast<uv_handle_t *>(sock), on_evt_listen_closed);
    }
    return;
  }

  // setup send buffer size
  proto->set_receive_buffer_limit(mgr->conf_.origin_conf.client().recv_buffer_size(), 0);
  proto->set_send_buffer_limit(mgr->conf_.origin_conf.client().send_buffer_size(), 0);

  int res = sess->accept_pipe(server);
  if (0 != res) {
    sess->close(static_cast<int>(close_reason_t::kServerBusy), 0, "server busy");
    return;
  }

  // check session number limit
  if (mgr->conf_.origin_conf.listen().max_client() > 0 &&
      mgr->reconnect_cache_.size() + mgr->actived_sessions_.size() >= mgr->conf_.origin_conf.listen().max_client()) {
    sess->close(static_cast<int>(close_reason_t::kServerBusy), 0, "server busy");
    return;
  }

  if (mgr->on_create_session_fn_) {
    mgr->on_create_session_fn_(sess.get(), sess->get_uv_stream());
  }

  // first idle timeout
  mgr->first_idle_.emplace_back();
  session_timeout_t &sess_timeout = mgr->first_idle_.back();
  sess_timeout.s = sess;
  if (mgr->conf_.origin_conf.client().first_idle_timeout().seconds() > 0) {
    sess_timeout.timeout =
        atfw::util::time::time_utility::get_now() + mgr->conf_.origin_conf.client().first_idle_timeout().seconds();
  } else {
    sess_timeout.timeout = atfw::util::time::time_utility::get_now() + 1;
  }
}

void session_manager::on_evt_listen_closed(uv_handle_t *handle) {
  // delete shared ptr
  listen_handle_ptr_t *ptr = reinterpret_cast<listen_handle_ptr_t *>(handle->data);
  delete ptr;
}
}  // namespace gateway
}  // namespace atframework
