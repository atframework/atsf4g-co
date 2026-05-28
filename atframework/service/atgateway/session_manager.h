// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atgateway/protocol/libatgw_server_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp.h>
#include <gsl/select-gsl.h>

#include <memory/lru_map.h>

#include <service_discovery_index/discovery_index.h>

#include <chrono>
#include <functional>
#include <list>

#include "session.h"

namespace atframework {
namespace gateway {
class session_manager {
 public:
  using crypto_conf_t = ::atframework::gateway::libatgw_protocol_sdk::crypto_conf_t;

  struct conf_t {
    size_t version;

    atframework::gateway::atgateway_cfg origin_conf;

    crypto_conf_t crypto;
  };

  struct session_with_timeout_t {
    session::ptr_t sess;
    std::chrono::system_clock::time_point timeout;
  };
  using session_map_t = atfw::util::memory::lru_map<session::id_t, session>;
  using session_timeout_map_t = atfw::util::memory::lru_map<session::id_t, session_with_timeout_t>;
  using create_proto_fn_t = std::function<std::unique_ptr< ::atframework::gateway::libatgw_protocol_api>()>;
  using on_create_session_fn_t = std::function<int(session *, uv_stream_t *)>;

 public:
  session_manager();
  ~session_manager();

  int init(::atfw::atapp::app *app_inst, create_proto_fn_t fn);
  /**
   * @brief listen all address in configure
   * @return the number of listened address
   */
  int listen_all();
  int listen(const char *address);
  int reset();
  int tick();
  int close(session::id_t sess_id, int32_t reason, int32_t sub_reason, atfw::util::nostd::string_view message,
            bool allow_reconnect = false);

  void reload();
  void cleanup();

  inline void *get_private_data() const { return private_data_; }
  inline void set_private_data(void *priv_data) { private_data_ = priv_data; }

  int post_data(::atbus::bus_id_t tid, ::atframework::gateway::server_message &message);
  int post_data(::atbus::bus_id_t tid, int32_t type, ::atframework::gateway::server_message &message);
  int post_data(::atbus::bus_id_t tid, int32_t type, gsl::span<const unsigned char> data);

  int post_data(const std::string &tname, ::atframework::gateway::server_message &message);
  int post_data(const std::string &tname, int32_t type, ::atframework::gateway::server_message &message);
  int post_data(const std::string &tname, int32_t type, gsl::span<const unsigned char> data);

  int push_data(session::id_t sess_id, const gsl::span<const unsigned char> data);
  int broadcast_data(gsl::span<const unsigned char> data);

  int set_session_router(session::id_t sess_id, ::atbus::bus_id_t router_node_id, const std::string &router_node_name);

  session::ptr_t find_session(session::id_t sess_id);

  inline conf_t &get_conf() { return conf_; }
  inline const conf_t &get_conf() const { return conf_; }

  inline on_create_session_fn_t get_on_create_session() const { return on_create_session_fn_; }
  inline void set_on_create_session(on_create_session_fn_t fn) { on_create_session_fn_ = std::move(fn); }

  int reconnect(session &new_sess, session::id_t old_sess_id);

  int active_session(session::ptr_t sess);

  void assign_default_router(session &sess) const;

  void remove_session_first_idle(session::id_t sess_id, const session *check_ptr);

  void remove_force_closed_session(const session *check_ptr);

  void update_force_closed_session(const session::ptr_t &sess_ptr);

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
  ::atfw::atapp::app *app_;
  conf_t conf_;

  create_proto_fn_t create_proto_fn_;
  on_create_session_fn_t on_create_session_fn_;

  using listen_handle_ptr_t = std::shared_ptr<uv_stream_t>;
  std::list<listen_handle_ptr_t> listen_handles_;
  session_map_t actived_sessions_;
  session_map_t reconnect_cache_;
  atfw::util::memory::lru_map<const session *, session_with_timeout_t> force_closed_sessions_;
  session_timeout_map_t first_idle_;
  session_timeout_map_t reconnect_timeout_;
  time_t last_tick_time_;

  component::service_discovery_index::ptr_t discovery_index_;

  void *private_data_;
};
}  // namespace gateway
}  // namespace atframework
