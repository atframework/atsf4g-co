// Copyright 2022 atframework
// Created by owent, on 2022-02-25

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <std/explicit_declare.h>

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_lru_cache_map.h>

class transaction_manager : public atfw::util::design_pattern::singleton<transaction_manager> {
 public:
  using transaction_lru_map_type =
      rpc::rpc_lru_cache_map<std::string, atfw::distributed_system::transaction_blob_storage>;
  using transaction_ptr_type = transaction_lru_map_type::cache_ptr_type;

 protected:
  transaction_manager();

 public:
  int tick();

  inline void stop() { is_exiting_ = true; }

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type save(rpc::context& ctx, transaction_ptr_type& data);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type create_transaction(
      rpc::context& ctx, atfw::distributed_system::transaction_blob_storage&& storage);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type mutable_transaction(
      rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata, transaction_ptr_type& out);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type try_commit(rpc::context& ctx, transaction_ptr_type& trans,
                                                                const std::string& participator_key);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type try_reject(rpc::context& ctx, transaction_ptr_type& trans,
                                                                const std::string& participator_key);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type try_commit(rpc::context& ctx, transaction_ptr_type& trans);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type try_reject(rpc::context& ctx, transaction_ptr_type& trans);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type try_remove(
      rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata);

 private:
  bool is_exiting_;
  time_t last_stat_timepoint_;
  transaction_lru_map_type lru_caches_;
};
