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

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/authsvr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory>

#include "app/handle_cs_rpc_authsvrclientservice.h"

namespace {
static std::shared_ptr<hello::config::authsvr_cfg> &get_server_cfg_pointer() {
  static std::shared_ptr<hello::config::authsvr_cfg> cfg = std::make_shared<hello::config::authsvr_cfg>();
  return cfg;
}
}  // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage)
const hello::config::authsvr_cfg &get_authsvr_cfg() { return *get_server_cfg_pointer(); }

class main_service_module : public atfw::atapp::module_impl {
 public:
  int reload() override {
    std::shared_ptr<hello::config::authsvr_cfg> cfg = std::make_shared<hello::config::authsvr_cfg>();
    get_app()->parse_configures_into(*cfg, "authsvr", "ATAPP_AUTHSVR");
    get_server_cfg_pointer().swap(cfg);
    return 0;
  }

  int init() override {
    {
      // register all router managers
      // router_player_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::authsvrclientservice::register_handles_for_authsvrclientservice);

    return 0;
  }

  const char *name() const override { return "main_service_module"; }
};

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // Common logic
  logic_server_common_module_configure logic_mod_conf;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(cs_msg_dispatcher::me());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
