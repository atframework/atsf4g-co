// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <stdint.h>
#include <cstddef>
#include <ctime>
#include <memory>

#include "uv.h"

#include "atgateway/protocols/libatgw_server_protocol.h"
#include "atgateway/protocols/v1/libatgw_protocol_sdk.h"
#include "libatbus.h"

namespace atframework {
namespace gateway {
class session_manager;
class session : public std::enable_shared_from_this<session> {
 public:
  struct limit_t {
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

    time_t hour_timepoint;
    time_t minute_timepoint;
    time_t update_handshake_timepoint;
  };

  using id_t = uint64_t;

  struct flag_t {
    enum type {
      EN_FT_INITED = 0x0001,
      EN_FT_HAS_FD = 0x0002,
      EN_FT_REGISTERED = 0x0004,
      EN_FT_RECONNECTED = 0x0008,
      EN_FT_WAIT_RECONNECT = 0x0010,
      EN_FT_CLOSING = 0x0020,
      EN_FT_CLOSING_FD = 0x0040,
      EN_FT_WRITING_FD = 0x0080,
    };
  };

  using ptr_t = std::shared_ptr<session>;

 public:
  session();
  ~session();

  bool check_flag(flag_t::type t) const;

  void set_flag(flag_t::type t, bool v);

  static ptr_t create(session_manager *, std::unique_ptr<atframework::gateway::libatgw_protocol_api> &);

  inline id_t get_id() const { return id_; };

  int accept_tcp(uv_stream_t *server);
  int accept_pipe(uv_stream_t *server);

  int init_new_session();

  int init_reconnect(session &sess);

  void on_alloc_read(size_t suggested_size, char *&out_buf, size_t &out_len);
  void on_read(int ssz, const char *buff, size_t len);
  int on_write_done(int status);

  int close(int reason);

  int close_with_manager(int reason, session_manager *mgr);

  int close_fd(int reason);

  int send_to_client(const void *data, size_t len);

  int send_to_server(::atframework::gw::ss_msg &msg);

  int send_to_server(::atframework::gw::ss_msg &msg, session_manager *mgr);

  atframework::gateway::libatgw_protocol_api *get_protocol_handle();
  const atframework::gateway::libatgw_protocol_api *get_protocol_handle() const;

  uv_stream_t *get_uv_stream();
  const uv_stream_t *get_uv_stream() const;

  int send_new_session();

 private:
  int send_remove_session();

  int send_remove_session(session_manager *mgr);

  static void on_evt_shutdown(uv_shutdown_t *req, int status);
  static void on_evt_closed(uv_handle_t *handle);

  void check_hour_limit(bool check_recv, bool check_send);
  void check_minute_limit(bool check_recv, bool check_send);
  void check_total_limit(bool check_recv, bool check_send);

 public:
  inline void *get_private_data() const noexcept { return private_data_; }
  inline void set_private_data(void *priv_data) noexcept { private_data_ = priv_data; }

  inline void set_router(::atbus::bus_id_t id, const std::string &node_name) {
    router_node_id_ = id;
    router_node_name_ = node_name;
  }

  inline ::atbus::bus_id_t get_router_id() const noexcept { return router_node_id_; }
  inline const std::string &get_router_name() const noexcept { return router_node_name_; }

  inline const std::string &get_peer_host() const noexcept { return peer_ip_; }
  inline int32_t get_peer_port() const noexcept { return peer_port_; }
  inline session_manager *get_manager() const noexcept { return owner_; }

 private:
  id_t id_;
  ::atbus::bus_id_t router_node_id_;
  std::string router_node_name_;
  session_manager *owner_;

  limit_t limit_;
  int flags_;
  union {
    uv_handle_t raw_handle_;
    uv_stream_t stream_handle_;
    uv_tcp_t tcp_handle_;
    uv_pipe_t unix_handle_;
    uv_udp_t udp_handle_;
  };
  uv_shutdown_t shutdown_req_;
  std::string peer_ip_;
  int32_t peer_port_;

  std::unique_ptr<atframework::gateway::libatgw_protocol_api> proto_;
  void *private_data_;
};
}  // namespace gateway
}  // namespace atframework
