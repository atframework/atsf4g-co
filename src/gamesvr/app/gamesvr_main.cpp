// Copyright 2021 atframework
// Created by owent

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <libatbus_protocol.h>
#include <time/time_utility.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <dispatcher/cs_msg_dispatcher.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <libatgw_server_protocol.h>

#include <config/server_frame_build_feature.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>
#include <router/router_player_manager.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "app/handle_cs_rpc_gamesvrclientservice.h"
#include "app/handle_ss_rpc_gamesvrservice.h"

#include "data/player.h"

class main_service_module : public atapp::module_impl {
 private:
  static router_player_cache::object_ptr_t create_player_fn(uint64_t user_id, uint32_t zone_id,
                                                            const std::string &openid) {
    return std::static_pointer_cast<player_cache>(player::create(user_id, zone_id, openid));
  }

 public:
  int init() override {
    {
      // register all router managers
      router_player_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::game::register_handles_for_gamesvrservice);
    INIT_CALL_FN(handle::gamesvrclientservice::register_handles_for_gamesvrclientservice);

    // reload will be triggered before init, so reload again here

    // setup how to create player
    router_player_manager::me()->set_create_object_fn(create_player_fn);

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

  app.add_module(cs_msg_dispatcher::me());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
