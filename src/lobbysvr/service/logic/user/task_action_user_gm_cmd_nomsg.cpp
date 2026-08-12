#include "task_action_user_gm_cmd_nomsg.h"

#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <config/compile_optimize.h>
#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>
#include <config/excel_config_wrapper.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <atframe/etcdcli/etcd_discovery.h>
#include <atframe/modules/etcd_module.h>
#include <cli/cmd_option.h>
#include <common/file_system.h>
#include <log/log_wrapper.h>

#include <data/user.h>
#include <data/user_cache.h>
#include <dispatcher/cs_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_manager.h>
#include <logic/logic_server_setup.h>
#include <logic/misc/logic_datetime_cache.h>
#include <logic/session_manager.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_common_types.h>
#include <rpc/telemetry/semantic_conventions.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

#include <logic/async_jobs/user_async_jobs_manager.h>
#include <logic/cache/global_cache_manager.h>
#include <logic/cache/user_cache_manager.h>
#include <logic/chat/user_chat_manager.h>

#define DECLARE_UTF8_LITERATE(X) ((const char *)u8##X)

namespace detail {
static ::util::cli::cmd_option_ci::ptr_type task_action_user_gm_cmd_cmd_set_;
}  // namespace detail

task_action_user_gm_cmd_nomsg::task_action_user_gm_cmd_nomsg(ctor_param_t &&param)
    : task_action_no_req_base(param), param_(param) {}
task_action_user_gm_cmd_nomsg::~task_action_user_gm_cmd_nomsg() {}

const char *task_action_user_gm_cmd_nomsg::name() const { return "task_action_user_gm_cmd_nomsg"; }

task_action_user_gm_cmd_nomsg::result_type task_action_user_gm_cmd_nomsg::operator()() {
  run_cmd();

  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (!task_type_trait::empty(async_task_)) {
    ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(get_shared_context(), async_task_));
    task_type_trait::reset_task(async_task_);
  }

  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_user_gm_cmd_nomsg::on_success() { return get_result(); }
int task_action_user_gm_cmd_nomsg::on_failed() { return get_result(); }

void task_action_user_gm_cmd_nomsg::run_cmd() {
  std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body = get_rsp();
  if (param_.args.empty()) {
    rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_GM_CMD_INVALID);
    return;
  }

  create_cmd_set();

  std::vector<const char *> params;
  params.reserve(param_.args.size());
  for (size_t i = 0; i < param_.args.size(); ++i) {
    params.push_back(param_.args[i].c_str());
  }

  detail::task_action_user_gm_cmd_cmd_set_->start(static_cast<int>(params.size()), &params[0], true,
                                                  reinterpret_cast<void *>(this));
}
task_action_user_gm_cmd_nomsg &task_action_user_gm_cmd_nomsg::get_self(::util::cli::cmd_option_list &params) {
  return *reinterpret_cast<task_action_user_gm_cmd_nomsg *>(params.get_ext_param());
}

std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> task_action_user_gm_cmd_nomsg::get_rsp() {
  if (!param_.rsp_body) {
    param_.rsp_body = atfw::memory::stl::make_shared<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp>();
  }
  return param_.rsp_body;
}

void task_action_user_gm_cmd_nomsg::set_async_task(task_type_trait::task_type t) { async_task_ = t; }

user_ptr_t task_action_user_gm_cmd_nomsg::get_user_cmd_params(::util::cli::cmd_option_list &params) {
  user_ptr_t user_inst = get_self(params).param_.user_inst;
  if (!user_inst) {
    std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body = get_self(params).get_rsp();
    rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    return nullptr;
  }
  return user_inst;
}

std::shared_ptr<rpc::context> task_action_user_gm_cmd_nomsg::create_child_context(
    ::util::cli::cmd_option_list &params) {
  return get_self(params).get_shared_context().create_shared_child();
}

void task_action_user_gm_cmd_nomsg::create_cmd_set() {
  if (detail::task_action_user_gm_cmd_cmd_set_) {
    return;
  }
  detail::task_action_user_gm_cmd_cmd_set_ = ::util::cli::cmd_option_ci::create();
  ::util::cli::cmd_option_ci::ptr_type cmd_set = detail::task_action_user_gm_cmd_cmd_set_;
  cmd_set->set_help_cmd_style(0);
  cmd_set->set_help_description_style(0);
  init_gm_cmd("@OnError", task_action_user_gm_cmd_nomsg::on_gm_cmd_invalid);
  init_gm_cmd("help", task_action_user_gm_cmd_nomsg::on_gm_cmd_help,
              DECLARE_UTF8_LITERATE("help Display help information, angle brackets in the following "
                                    "commands are "
                                    "<required> parameters, and square brackets are [optional] "
                                    "parameters.."));
}

bool task_action_user_gm_cmd_nomsg::check_params_number(::util::cli::cmd_option_list &params, size_t param_num) {
  if (params.get_params_number() < param_num) {
    task_action_user_gm_cmd_nomsg &self = get_self(params);
    std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body = self.get_rsp();

    std::stringstream ss;
    ss << "Command ";
    const typename util::cli::cmd_option_list::cmd_array_type &cmd_arr = params.get_cmd_array();
    size_t arr_sz = cmd_arr.size();
    for (size_t i = 1; i < arr_sz; ++i) {
      if (!cmd_arr[i].first.empty()) {
        ss << cmd_arr[i].first << " ";
      }
    }
    ss << "require " << param_num << " parameter(s), but we only got" << params.get_params_number() << std::endl;
    rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_GM_CMD_PARAM_NUMBER);
    rsp_body->set_result_message(ss.str());
    return false;
  }
  return true;
}

void task_action_user_gm_cmd_nomsg::init_gm_cmd(
    const std::string &cmd_name,
    void (*handler)(user_ptr_t user_inst, std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp,
                    ::util::cli::cmd_option_list &params),
    const std::string &help_msg) {
  create_cmd_set();
  auto result =
      detail::task_action_user_gm_cmd_cmd_set_->bind_cmd(cmd_name, [handler](::util::cli::cmd_option_list &params) {
        task_action_user_gm_cmd_nomsg &self = get_self(params);
        std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body = self.get_rsp();
        user_ptr_t user_inst = get_user_cmd_params(params);
        if (!user_inst) {
          FWLOGERROR("can not find user of task_action_user_gm_cmd_nomsg");
          return;
        }
        handler(user_inst, rsp_body, params);
      });
  if (!help_msg.empty()) {
    result->set_help_msg(help_msg.c_str());
  }
}

void task_action_user_gm_cmd_nomsg::on_gm_cmd_help(user_ptr_t,
                                                   std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body,
                                                   ::util::cli::cmd_option_list &) {
  std::stringstream ss;

  ss << "Usage: <command> [command paraters...]" << std::endl;
  if (detail::task_action_user_gm_cmd_cmd_set_) {
    ss << detail::task_action_user_gm_cmd_cmd_set_->get_help_msg() << std::endl;
  }
  rsp_body->set_result_message(ss.str());
}

void task_action_user_gm_cmd_nomsg::on_gm_cmd_invalid(
    user_ptr_t, std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp_body,
    ::util::cli::cmd_option_list &params) {
  std::stringstream ss;
  ss << "Cmd Invalid." << std::endl;
  const typename util::cli::cmd_option_list::cmd_array_type &cmd_arr = params.get_cmd_array();
  size_t arr_sz = cmd_arr.size();
  for (size_t i = 1; i < arr_sz; ++i) {
    if (!cmd_arr[i].first.empty()) {
      ss << cmd_arr[i].first << " ";
    }
  }

  if (detail::task_action_user_gm_cmd_cmd_set_) {
    detail::task_action_user_gm_cmd_cmd_set_->dump(ss, "") << std::endl;
  }

  rsp_body->set_result_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_GM_CMD_INVALID);
  rsp_body->set_result_message(ss.str());
}