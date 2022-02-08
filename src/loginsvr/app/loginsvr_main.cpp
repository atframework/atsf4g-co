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
#include <libatgw_server_protocol.h>

#include <config/excel/config_manager.h>
#include <config/logic_config.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "app/handle_cs_rpc_loginsvrclientservice.h"

class main_service_module : public atapp::module_impl {
 public:
  int init() override {
    {
      // register all router managers
      // router_player_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::loginsvrclientservice::register_handles_for_loginsvrclientservice);

    return 0;
  }

  const char *name() const override { return "main_service_module"; }
};

int main(int argc, char *argv[]) {
  atapp::app app;

  // project directory
  {
    std::string proj_dir;
    util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // Common logic
  logic_server_common_module_configure logic_mod_conf;
  logic_mod_conf.enable_watch_battlesvr = false;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(std::make_shared<main_service_module>());
  app.add_module(cs_msg_dispatcher::me());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
