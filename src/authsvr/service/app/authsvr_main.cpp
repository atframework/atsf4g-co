// Copyright 2021 atframework
// Created by owent

#include <common/file_system.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <libatbus_protocol.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <dispatcher/cs_msg_dispatcher.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>
#include <logic/session_manager.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/authsvr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>

#include <service_discovery_index/discovery_index.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "app/authsvr_helper.h"
#include "app/handle_cs_rpc_authsvrclientservice.atfw.gen.h"
#include "logic/algorithm/auth_account_generator.h"

namespace {
class main_service_module;

main_service_module *g_main_service_module = nullptr;

class main_service_module : public atfw::atapp::module_impl {
 public:
  main_service_module() { g_main_service_module = this; }

  ~main_service_module() {
    if (g_main_service_module == this) {
      g_main_service_module = nullptr;
    }
  }

  int init() override {
    {
      // register all router managers
      // router_user_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::authsvrclientservice::register_handles_for_authsvrclientservice);
    INIT_CALL(session_manager);

    discovery_index_ = atfw::component::service_discovery_index::create(get_app()->get_service_discovery_module());
    discovery_index_->initialize();

    return 0;
  }

  int tick() override {
    int ret = 0;
    ret += session_manager::me()->proc();
    return ret;
  }

  int reload() override {
    if (discovery_index_) {
      discovery_index_->reload();
    }
    return 0;
  }

  void cleanup() override {
    if (discovery_index_) {
      discovery_index_->cleanup();
      discovery_index_.reset();
    }
  }

  const char *name() const override { return "main_service_module"; }

  // 自定义指令：预生成一批内部鉴权账号写入 login_auth 表，关闭注册后也能用预置账号密码登录
  static int cmd_gen_accounts(atfw::util::cli::callback_param params) {
    constexpr const char *kUsage =
        "auth-gen-accounts <count> <open-id prefix> [start index]  pre-generate internal auth "
        "accounts (default: prefix=devacc, start index=1)";

    if (params.get_params_number() < 2) {
      ::atfw::atapp::app::add_custom_command_rsp(params, kUsage);
      return 0;
    }

    int64_t count = params[0]->to_int64();
    if (count <= 0 || count > 10000) {
      ::atfw::atapp::app::add_custom_command_rsp(params, "count must be in range [1, 10000]");
      return 0;
    }

    std::string open_id_prefix = params[1]->to_cpp_string();

    int64_t start_index = 1;
    if (params.get_params_number() >= 3) {
      start_index = params[2]->to_int64();
      if (start_index <= 0) {
        ::atfw::atapp::app::add_custom_command_rsp(params, "start index must be positive");
        return 0;
      }
    }

    uint32_t account_type = static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_ATI_ACCOUNT_INTERNAL);

    const auto &server_cfg =
        logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::authsvr_cfg>();
    if (!server_cfg.internal_auth().enable()) {
      FWLOGWARNING(
          "auth-gen-accounts: internal_auth is disabled, generated accounts can not login before it is enabled");
    }

    // 指令回调是同步的，DB 写入必须异步执行；指令本身只回报“已调度”，账号明细写入日志
    rpc::context ctx{rpc::context::create_without_task()};
    auto invoke_result = rpc::async_invoke(
        ctx, "authsvr.generate_accounts",
        [count, open_id_prefix = std::move(open_id_prefix), start_index,
         account_type](rpc::context &child_ctx) -> rpc::result_code_type {
          std::vector<auth_generated_account> generated;
          int res = RPC_AWAIT_CODE_RESULT(auth_generate_accounts(child_ctx, account_type, open_id_prefix,
                                                                 static_cast<uint64_t>(start_index),
                                                                 static_cast<uint32_t>(count), generated));
          if (res < 0) {
            FWLOGERROR("auth-gen-accounts stopped with error code {}, {} accounts created before failure", res,
                       generated.size());
            RPC_RETURN_CODE(res);
          }

          FWLOGINFO("auth-gen-accounts created {} accounts, open_id and password are logged above", generated.size());
          RPC_RETURN_CODE(0);
        });

    if (invoke_result.is_error()) {
      FWLOGERROR("auth-gen-accounts failed to schedule async task, error code: {}", *invoke_result.get_error());
      ::atfw::atapp::app::add_custom_command_rsp(params, "auth-gen-accounts failed to schedule async task");
      return 0;
    }

    ::atfw::atapp::app::add_custom_command_rsp(
        params, "auth-gen-accounts scheduled, see service log for generated open_id and password");
    return 0;
  }

  const atfw::component::service_discovery_index::ptr_t &get_discovery_index() const noexcept {
    return discovery_index_;
  }

 private:
  atfw::component::service_discovery_index::ptr_t discovery_index_;
};

static int app_handle_on_receive_request(atfw::atapp::app &, const atfw::atapp::app::message_sender_t &source,
                                         const atfw::atapp::app::message_t &msg) {
  if (0 == source.id) {
    FWLOGERROR("receive a message from unknown source or invalid body case");
    return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  int ret = 0;
  switch (msg.type) {
    case static_cast<int32_t>(::atfw::component::service_type::kAtGateway): {
      ret = cs_msg_dispatcher::me()->dispatch(source, msg);
      break;
    }

    case static_cast<int32_t>(::atfw::component::message_type::kInServerMessage): {
      ret = ss_msg_dispatcher::me()->dispatch(source, msg);
      break;
    }

    default: {
      FWLOGERROR("receive a message of invalid type: {}", msg.type);
      break;
    }
  }

  return ret;
}
}  // namespace

atfw::component::service_discovery_index::ptr_t authsvr_get_service_discovery_index() noexcept {
  if (g_main_service_module != nullptr) {
    return g_main_service_module->get_discovery_index();
  }

  return nullptr;
}

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app &app_, logic_config & /*cfg*/, logic_config::server_instance_config_ptr &to_) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::config::authsvr_cfg>();
        app_.parse_configures_into(*config_ptr, "authsvr", "ATAPP_AUTHSVR");
        to_ = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });

  // Common logic
  logic_server_common_module_configure logic_mod_conf;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }
  app.set_evt_on_forward_request(app_handle_on_receive_request);

  app.add_module(cs_msg_dispatcher::me());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  // 自定义指令注册示例：handler 为 static 成员函数，指令通过 atapp 的控制台/管理通道下发
  app.get_command_manager()
      ->bind_cmd("auth-gen-accounts", &main_service_module::cmd_gen_accounts)
      ->set_help_msg(
          "auth-gen-accounts <count> [open-id prefix] [start index] [account type]    pre-generate internal auth "
          "accounts");

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
