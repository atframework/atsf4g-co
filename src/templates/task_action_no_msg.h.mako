## -*- coding: utf-8 -*-
<%!
import time
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${local_vcs_user_name} on ${time.strftime("%Y-%m-%d %H:%M:%S")}

#ifndef LOGIC_ACTION_${task_class_name.upper()}_H
#define LOGIC_ACTION_${task_class_name.upper()}_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

class ${task_class_name} : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit ${task_class_name}(ctor_param_t&& param);
  ~${task_class_name}();

  const char *name() const override;

  int operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  ctor_param_t param_;
};

#endif  //LOGIC_ACTION_${task_class_name.upper()}_H
