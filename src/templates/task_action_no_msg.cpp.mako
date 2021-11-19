## -*- coding: utf-8 -*-
<%!
import time
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${local_vcs_user_name} on ${time.strftime("%Y-%m-%d %H:%M:%S")}

#include "${task_class_name}.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <std/explicit_declare.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

${task_class_name}::${task_class_name}(
  ctor_param_t&& param)
  : task_action_no_req_base(param), param_(param) {}
${task_class_name}::~${task_class_name}() {}

const char *${task_class_name}::name() const {
  return "${task_class_name}";
}

${task_class_name}::result_type ${task_class_name}::operator()() {
  // Maybe need to call 
  // set_user_key(param_.user_id, param_.zone_id); 
  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int ${task_class_name}::on_success() { return get_result(); }

int ${task_class_name}::on_failed() { return get_result(); }
