// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef DTMQ_PROXY_SERVICE_API
#  define DTMQ_PROXY_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_transfer_channel : public task_action_ss_rpc_base<atframework::dtmq::SSChannelTransferChannelReq,
                                                                    atframework::dtmq::SSChannelTransferChannelRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::dtmq::SSChannelTransferChannelReq,
                                            atframework::dtmq::SSChannelTransferChannelRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  DTMQ_PROXY_SERVICE_API explicit task_action_transfer_channel(dispatcher_start_data_type&& param);
  DTMQ_PROXY_SERVICE_API ~task_action_transfer_channel();

  DTMQ_PROXY_SERVICE_API const char* name() const override;

  DTMQ_PROXY_SERVICE_API result_type operator()() override;

  DTMQ_PROXY_SERVICE_API int on_success() override;
  DTMQ_PROXY_SERVICE_API int on_failed() override;
};
