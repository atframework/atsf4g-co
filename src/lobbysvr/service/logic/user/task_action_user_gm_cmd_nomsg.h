#pragma once

#include <cli/cmd_option_list.h>
#include <dispatcher/task_action_no_req_base.h>

#include <dispatcher/task_type_traits.h>

#include <data/user_type_define.h>

#include <memory>
#include <string>
#include <vector>

namespace PROJECT_NAMESPACE_ID {
class SCUserGMCommandRsp;
}

class task_action_user_gm_cmd_nomsg : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    user_ptr_t user_inst;
    std::vector<std::string> args;
    std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_user_gm_cmd_nomsg(ctor_param_t&& param);
  ~task_action_user_gm_cmd_nomsg();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

  // 管理Manager注册GM接口
  static bool check_params_number(::util::cli::cmd_option_list& params, size_t param_num);
  static void init_gm_cmd(const std::string& cmd_name,
                          void (*handler)(std::shared_ptr<rpc::context> ctx, user_ptr_t user_inst,
                                          std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp,
                                          ::util::cli::cmd_option_list& params),
                          const std::string& help_msg = std::string());

 private:
  void run_cmd();
  static task_action_user_gm_cmd_nomsg& get_self(::util::cli::cmd_option_list& params);
  std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> get_rsp();
  void set_async_task(task_type_trait::task_type t);
  static user_ptr_t get_user_cmd_params(::util::cli::cmd_option_list& params);

  static void on_gm_cmd_help(std::shared_ptr<rpc::context> ctx, user_ptr_t user_inst,
                             std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body,
                             ::util::cli::cmd_option_list& params);
  static void on_gm_cmd_invalid(std::shared_ptr<rpc::context> ctx, user_ptr_t user_inst,
                                std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body,
                                ::util::cli::cmd_option_list& params);

 private:
  static void create_cmd_set();

 private:
  task_type_trait::task_type async_task_;
  ctor_param_t param_;
};
