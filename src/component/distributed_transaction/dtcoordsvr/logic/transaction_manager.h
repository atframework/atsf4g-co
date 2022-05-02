// Copyright 2022 atframework
// Created by owent, on 2022-02-25

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_lru_cache_map.h>

#include <stdint.h>
#include <cstddef>
#include <memory>
#include <unordered_map>

class transaction_manager : public util::design_pattern::singleton<transaction_manager> {
 public:
  using transaction_lru_map_type =
      rpc::rpc_lru_cache_map<std::string, atframework::distributed_system::transaction_blob_storage>;
  using transaction_ptr_type = transaction_lru_map_type::cache_ptr_type;

 protected:
  transaction_manager();

 public:
  int tick();

  inline void stop() { is_exiting_ = true; }

  rpc::result_code_type save(rpc::context& ctx, transaction_ptr_type& data);

  rpc::result_code_type create_transaction(rpc::context& ctx,
                                           atframework::distributed_system::transaction_blob_storage&& storage);

  rpc::result_code_type mutable_transaction(rpc::context& ctx,
                                            const atframework::distributed_system::transaction_metadata& metadata,
                                            transaction_ptr_type& out);

  rpc::result_code_type try_commit(rpc::context& ctx, transaction_ptr_type& trans, const std::string& participator_key);

  rpc::result_code_type try_reject(rpc::context& ctx, transaction_ptr_type& trans, const std::string& participator_key);

  rpc::result_code_type try_commit(rpc::context& ctx, transaction_ptr_type& trans);

  rpc::result_code_type try_reject(rpc::context& ctx, transaction_ptr_type& trans);

  rpc::result_code_type try_remove(rpc::context& ctx,
                                   const atframework::distributed_system::transaction_metadata& metadata);

 private:
  bool is_exiting_;
  time_t last_stat_timepoint_;
  transaction_lru_map_type lru_caches_;
};
