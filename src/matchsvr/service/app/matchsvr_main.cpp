// Copyright 2026 atframework

#include <atframe/atapp.h>
#include <common/file_system.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/match_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "app/handle_ss_rpc_matchsvrservice.atfw.gen.h"
#include "app/handle_ss_rpc_orbitsvrmatchsvrservice.atfw.gen.h"
#include "logic/matching/matching_manager.h"

namespace {
class main_service_module : public atfw::atapp::module_impl {
 public:
  // 注册匹配 RPC，并初始化唯一的运行时管理器。
  int init() override {
    INIT_CALL_FN(handle::matching::register_handles_for_matchsvrservice);
    INIT_CALL_FN(handle::orbit::register_handles_for_orbitsvrmatchsvrservice);

    return matching_manager::me()->init();
  }

  // 驱动搜索超时与终态房间回收。
  int tick() override { return matching_manager::me()->tick(); }

  const char* name() const override { return "main_service_module"; }
};
}  // namespace

int main(int argc, char* argv[]) {
  atfw::atapp::app app;
  {
    std::string project_directory;
    atfw::util::file_system::dirname(__FILE__, 0, project_directory, 5);
    atfw::util::log::log_formatter::set_project_directory(project_directory.c_str(), project_directory.size());
  }

  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app& app_, logic_config& /*cfg*/, logic_config::server_instance_config_ptr& to_) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
        app_.parse_configures_into(*config_ptr, "matchsvr", "ATAPP_MATCHSVR");
        to_ = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });

  logic_server_common_module_configure logic_module_config;
  if (logic_server_setup_common(app, logic_module_config) < 0) {
    return -1;
  }
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(atfw::memory::stl::make_shared<main_service_module>());
  return app.run(uv_default_loop(), argc, const_cast<const char**>(argv), nullptr);
}
