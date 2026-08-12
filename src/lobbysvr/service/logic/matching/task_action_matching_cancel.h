// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.match.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/task_action_cs_req_base.h>

#ifndef GAMECLIENT_SERVICE_API
#  define GAMECLIENT_SERVICE_API UTIL_SYMBOL_VISIBLE
#endif

class task_action_matching_cancel
    : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSMatchingCancelReq,
                                     PROJECT_NAMESPACE_ID::SCMatchingCancelRsp> {
 public:
  using base_type = task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSMatchingCancelReq,
                                            PROJECT_NAMESPACE_ID::SCMatchingCancelRsp>;
  using base_type::operator();

  GAMECLIENT_SERVICE_API explicit task_action_matching_cancel(dispatcher_start_data_type&& param);
  GAMECLIENT_SERVICE_API ~task_action_matching_cancel() override;
  GAMECLIENT_SERVICE_API const char* name() const override;
  GAMECLIENT_SERVICE_API result_type operator()() override;
  GAMECLIENT_SERVICE_API int on_success() override;
  GAMECLIENT_SERVICE_API int on_failed() override;
};
