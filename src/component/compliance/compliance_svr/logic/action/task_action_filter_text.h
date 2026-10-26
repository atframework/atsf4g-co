// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/compliance_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API
#  define ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_filter_text : public task_action_ss_rpc_base<atframework::compliance::SSFilterTextReq,
                                                               atframework::compliance::SSFilterTextRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<atframework::compliance::SSFilterTextReq, atframework::compliance::SSFilterTextRsp>;
  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API explicit task_action_filter_text(dispatcher_start_data_type&& param);
  ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API ~task_action_filter_text() override;

  ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API const char* name() const override;

  ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API result_type operator()() override;

  ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API int on_success() override;
  ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API int on_failed() override;
};
