// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-04-24 21:34:18

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef GAME_SERVICE_API
#  define GAME_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_object_cache_meta_sync
    : public task_action_ss_rpc_base<hello::SSObjectCacheMetaSync, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<hello::SSObjectCacheMetaSync, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  GAME_SERVICE_API explicit task_action_object_cache_meta_sync(dispatcher_start_data_type&& param);
  GAME_SERVICE_API ~task_action_object_cache_meta_sync();

  GAME_SERVICE_API const char* name() const override;

  GAME_SERVICE_API result_type operator()() override;

  GAME_SERVICE_API int on_success() override;
  GAME_SERVICE_API int on_failed() override;
};
