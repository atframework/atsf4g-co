// Copyright 2026 atframework

#include <common/file_system.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <libatbus_protocol.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <dispatcher/ss_msg_dispatcher.h>

#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/orbit_agent_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/handle_ss_rpc_clienttoagentservice.atfw.gen.h"
#include "app/handle_ss_rpc_controllertoagentservice.atfw.gen.h"
#include "logic/orbit_agent_manager.h"

class main_service_module : public atapp::module_impl, public std::enable_shared_from_this<main_service_module> {
 public:
  int init() override {
    {
      // register all router managers
    }

    INIT_CALL_FN(handle::clienttoagentservice::register_handles_for_clienttoagentservice);
    INIT_CALL_FN(handle::controllertoagentservice::register_handles_for_controllertoagentservice);
    INIT_CALL(orbit_agent_manager, get_app());
    return 0;
  }

  int stop() override { return orbit_agent_manager::me()->stop(); }
  void cleanup() override { orbit_agent_manager::me()->cleanup(); }

  const char *name() const override { return "main_service_module"; }

  int tick() override {
    int ret = 0;
    orbit_agent_manager::me()->tick();
    return ret;
  }
};

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app &app_, logic_config & /*cfg*/, logic_config::server_instance_config_ptr &to_) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<orbit::config::orbit_agent_cfg>();
        app_.parse_configures_into(*config_ptr, "orbit_agent");
        to_ = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });

  logic_server_common_module_configure logic_mod_conf;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(ss_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
