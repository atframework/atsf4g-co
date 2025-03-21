// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <list>
#include <memory>

#include "dispatcher/task_action_base.h"

class ATFW_UTIL_SYMBOL_VISIBLE task_action_no_req_base : public task_action_base {
 public:
  using base_type = task_action_base;
  using result_type = base_type::result_type;

  struct ctor_param_t {
    rpc::context *caller_context;

    ATFW_UTIL_FORCEINLINE ctor_param_t() noexcept : caller_context(nullptr) {}
  };

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  SERVER_FRAME_API explicit task_action_no_req_base(const ctor_param_t &param);
  SERVER_FRAME_API ~task_action_no_req_base();

  SERVER_FRAME_API std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  SERVER_FRAME_API const char *get_type_name() const override;

  SERVER_FRAME_API static void add_prepare_handle(rpc::result_code_type (*fn)(rpc::context &,
                                                                              task_action_no_req_base &));

  SERVER_FRAME_API result_type hook_run() override;

  SERVER_FRAME_API rpc::context::inherit_options get_inherit_option() const noexcept override;
  SERVER_FRAME_API rpc::context::trace_start_option get_trace_option() const noexcept override;

 protected:
  SERVER_FRAME_API void send_response() override;

 private:
  SERVER_FRAME_API static dispatcher_start_data_type make_from_context(const ctor_param_t &param) noexcept;

  static std::list<rpc::result_code_type (*)(rpc::context &, task_action_no_req_base &)> prepare_handles_;
};
