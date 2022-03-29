// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <memory>

#include "router/router_object_base.h"

class router_manager_base {
 public:
  using key_t = router_object_base::key_t;

 protected:
  explicit router_manager_base(uint32_t type_id);

 public:
  virtual ~router_manager_base();
  virtual const char *name() const = 0;

  inline uint32_t get_type_id() const { return type_id_; }
  virtual std::shared_ptr<router_object_base> get_base_cache(const key_t &key) const = 0;
  virtual rpc::result_code_type mutable_cache(rpc::context &ctx, std::shared_ptr<router_object_base> &out,
                                              const key_t &key, void *priv_data) = 0;
  virtual rpc::result_code_type mutable_object(rpc::context &ctx, std::shared_ptr<router_object_base> &out,
                                               const key_t &key, void *priv_data) = 0;

  virtual rpc::result_code_type remove_cache(rpc::context &ctx, const key_t &key,
                                             std::shared_ptr<router_object_base> cache, void *priv_data) = 0;
  virtual rpc::result_code_type remove_object(rpc::context &ctx, const key_t &key,
                                              std::shared_ptr<router_object_base> cache, void *priv_data) = 0;

  virtual bool is_auto_mutable_object() const;
  virtual bool is_auto_mutable_cache() const;
  virtual uint64_t get_default_router_server_id(const key_t &key) const;

  rpc::result_code_type send_msg(rpc::context &ctx, router_object_base &obj, atframework::SSMsg &&msg,
                                 uint64_t &sequence);
  rpc::result_code_type send_msg(rpc::context &ctx, const key_t &key, atframework::SSMsg &&msg, uint64_t &sequence);

  inline size_t size() const { return stat_size_; }

  inline bool is_closing() const { return is_closing_; }

  virtual void on_stop();

  virtual rpc::result_code_type pull_online_server(rpc::context &ctx, const key_t &key, uint64_t &router_svr_id,
                                                   uint64_t &router_svr_ver);

 protected:
  rpc::result_code_type send_msg_raw(rpc::context &ctx, router_object_base &obj, atframework::SSMsg &&msg,
                                     uint64_t &sequence);

 protected:
  size_t stat_size_;

 private:
  uint32_t type_id_;
  bool is_closing_;
};
