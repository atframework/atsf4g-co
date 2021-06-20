#ifndef ATFRAME_SERVICE_ATGATEWAY_SESSION_MANAGER_H
#define ATFRAME_SERVICE_ATGATEWAY_SESSION_MANAGER_H

#pragma once

#include <std/functional.h>
#include <list>
#include <map>

#if (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(_MSC_VER) && _MSC_VER >= 1600)
#  include <unordered_map>
#  define ATFRAME_GATEWAY_AUTO_MAP(...) std::unordered_map<__VA_ARGS__>

#else
#  include <map>
#  define ATFRAME_GATEWAY_AUTO_MAP(...) std::map<__VA_ARGS__>
#endif

#include "session.h"

namespace atframe {
namespace gateway {
class session_manager {
 public:
  struct client_limit_t {
    size_t total_recv_bytes;
    size_t total_send_bytes;
    size_t hour_recv_bytes;
    size_t hour_send_bytes;
    size_t minute_recv_bytes;
    size_t minute_send_bytes;

    size_t total_recv_times;
    size_t total_send_times;
    size_t hour_recv_times;
    size_t hour_send_times;
    size_t minute_recv_times;
    size_t minute_send_times;

    size_t max_client_number;
  };

  struct lister_conf_t {
    std::vector<std::string> address;
    std::string type;
    int backlog;
  };

  typedef ::atframe::gateway::libatgw_proto_inner_v1::crypt_conf_t crypt_conf_t;

  struct conf_t {
    size_t version;
    client_limit_t limits;
    lister_conf_t listen;
    time_t reconnect_timeout;
    time_t first_idle_timeout;
    size_t send_buffer_size;
    ::atbus::node::bus_id_t default_router;

    crypt_conf_t crypt;
  };

  typedef ATFRAME_GATEWAY_AUTO_MAP(session::id_t, session::ptr_t) session_map_t;
  typedef std::function<std::unique_ptr< ::atframe::gateway::proto_base>()> create_proto_fn_t;
  typedef std::function<int(session *, uv_stream_t *)> on_create_session_fn_t;

 public:
  session_manager();
  ~session_manager();

  int init(::atbus::node *bus_node, create_proto_fn_t fn);
  /**
   * @brief listen all address in configure
   * @return the number of listened address
   */
  int listen_all();
  int listen(const char *address);
  int reset();
  int tick();
  int close(session::id_t sess_id, int reason, bool allow_reconnect = false);

  inline void *get_private_data() const { return private_data_; }
  inline void set_private_data(void *priv_data) { private_data_ = priv_data; }

  int post_data(::atbus::node::bus_id_t tid, ::atframe::gw::ss_msg &msg);
  int post_data(::atbus::node::bus_id_t tid, int type, ::atframe::gw::ss_msg &msg);
  int post_data(::atbus::node::bus_id_t tid, int type, const void *buffer, size_t s);

  int push_data(session::id_t sess_id, const void *buffer, size_t s);
  int broadcast_data(const void *buffer, size_t s);

  int set_session_router(session::id_t sess_id, ::atbus::node::bus_id_t router);

  inline conf_t &get_conf() { return conf_; }
  inline const conf_t &get_conf() const { return conf_; }

  inline on_create_session_fn_t get_on_create_session() const { return on_create_session_fn_; }
  inline void set_on_create_session(on_create_session_fn_t fn) { on_create_session_fn_ = fn; }

  int reconnect(session &new_sess, session::id_t old_sess_id);

  int active_session(session::ptr_t sess);

 private:
  static void on_evt_accept_tcp(uv_stream_t *server, int status);
  static void on_evt_accept_pipe(uv_stream_t *server, int status);

  static void on_evt_listen_closed(uv_handle_t *handle);

 private:
  struct session_timeout_t {
    time_t timeout;
    session::ptr_t s;
  };

  uv_loop_t *evloop_;
  ::atbus::node *app_node_;
  conf_t conf_;

  create_proto_fn_t create_proto_fn_;
  on_create_session_fn_t on_create_session_fn_;

  typedef std::shared_ptr<uv_stream_t> listen_handle_ptr_t;
  std::list<listen_handle_ptr_t> listen_handles_;
  session_map_t actived_sessions_;
  std::list<session_timeout_t> first_idle_;
  session_map_t reconnect_cache_;
  std::list<session_timeout_t> reconnect_timeout_;
  time_t last_tick_time_;
  void *private_data_;
};
}  // namespace gateway
}  // namespace atframe

#endif