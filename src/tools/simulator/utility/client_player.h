// Copyright 2021 atframework
// Created by owent on 2016-10-11.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_PLAYER_H
#define ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_PLAYER_H

#pragma once

#include <config/compiler_features.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <libatgw_inner_v1_c.h>

#include <simulator_player_impl.h>

#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

class client_player : public simulator_player_impl {
 public:
  using libuv_ptr_t = simulator_player_impl::libuv_ptr_t;

 public:
  static void init_handles();

  client_player();
  virtual ~client_player();

  int connect(const std::string &host, int port) override;

  int on_connected(libuv_ptr_t net, int status) override;
  void on_alloc(libuv_ptr_t net, size_t suggested_size, uv_buf_t *buf) override;
  void on_read_data(libuv_ptr_t net, ssize_t nread, const uv_buf_t *buf) override;
  void on_read_message(libuv_ptr_t net, const void *buffer, size_t sz) override;
  void on_written_data(libuv_ptr_t net, int status) override;
  int on_write_message(libuv_ptr_t net, void *buffer, uint64_t sz) override;
  int on_disconnected(libuv_ptr_t net) override;

  void on_close() override;
  void on_closed() override;

  inline const PROJECT_NAMESPACE_ID::DAccountData &get_account() const { return account_; }
  inline PROJECT_NAMESPACE_ID::DAccountData &get_account() { return account_; }

  inline int32_t get_system_id() const { return system_id_; }
  inline void set_system_id(int32_t id) { system_id_ = id; }

  inline const std::string &get_package_version() const { return package_version_; }
  inline void set_package_version(const std::string &id) { package_version_ = id; }

  inline const std::string &get_resource_version() const { return resource_version_; }
  inline void set_resource_version(const std::string &id) { resource_version_ = id; }

  inline const std::string &get_protocol_version() const { return protocol_version_; }
  inline void set_protocol_version(const std::string &id) { protocol_version_ = id; }

  inline uint64_t get_user_id() const { return user_id_; }
  inline void set_user_id(uint64_t id) { user_id_ = id; }

  uint32_t alloc_sequence();

  inline const std::string &get_gamesvr_addr() const { return gamesvr_addr_; }
  inline void set_gamesvr_addr(const std::string &addr) { gamesvr_addr_ = addr; }

  inline const std::string &get_login_code() const { return login_code_; }
  inline void set_login_code(const std::string &code) { login_code_ = code; }

  inline int get_gamesvr_index() const { return gamesvr_index_; }
  inline void set_gamesvr_index(int index) { gamesvr_index_ = index; }

  libatgw_inner_v1_c_context mutable_proto_context(libuv_ptr_t net);
  void destroy_proto_context(libuv_ptr_t net);

  libuv_ptr_t find_network(libatgw_inner_v1_c_context ctx);
  using simulator_player_impl::find_network;

  void connect_done(libatgw_inner_v1_c_context ctx);

  inline bool is_connecting() const { return is_connecting_; }

  bool lua_run_code(const std::string &code);

  bool lua_run_file(const std::string &file_path);

  const std::string &mutable_lua_env_table();

 private:
  std::map<uint32_t, libatgw_inner_v1_c_context> proto_handles_;
  PROJECT_NAMESPACE_ID::DAccountData account_;
  int32_t system_id_;
  std::string package_version_;
  std::string resource_version_;
  std::string protocol_version_;
  uint64_t user_id_;

  uint32_t sequence_;
  std::string gamesvr_addr_;
  std::string login_code_;
  int gamesvr_index_;

  std::vector<std::vector<unsigned char> > pending_msg_;
  bool is_connecting_;

  std::string lua_env_path_;
};

#endif  // ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_PLAYER_H
