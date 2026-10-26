// Copyright 2026 atframework

#include "logic/action/task_action_filter_text.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility>

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API task_action_filter_text::task_action_filter_text(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API task_action_filter_text::~task_action_filter_text() {}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API const char* task_action_filter_text::name() const {
  return "task_action_filter_text";
}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API task_action_filter_text::result_type
task_action_filter_text::operator()() {
  // TODO(owent): 接入敏感词过滤；实现前不填充业务结果。
  set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOT_SUPPORT);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API int task_action_filter_text::on_success() { return get_result(); }

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API int task_action_filter_text::on_failed() { return get_result(); }
