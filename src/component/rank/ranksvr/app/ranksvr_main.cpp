#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <common/file_system.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <libatbus_protocol.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>

#include <config/server_frame_build_feature.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

#include <logic/rank_manager.h>
#include <logic/rank_mirror_global.h>

#include "app/handle_ss_rpc_ranksvrservice.h"

class main_service_module : public atapp::module_impl, public std::enable_shared_from_this<main_service_module> {
 public:
  int init() override {
    {
      // register all router managers
    }

    INIT_CALL_FN(handle::rank::register_handles_for_ranksvrservice);

    // INIT_CALL(rank_settlement_manager);
    INIT_CALL(rank_manager);
    INIT_CALL(rank_mirror_global);

    // logic_server_common_module *logic_module = logic_server_last_common_module();
    // if (logic_module) {
    //   logic_module->add_prestop_callback(
    //       []() -> int32_t {
    //         rank_manager::me()->stop();
    //         return 0;
    //       },
    //       "rank_manager stop");
    //   logic_module->init_prestop_check_callback([]() -> bool { return rank_manager::me()->is_closed(); });
    // }
    // INIT_MODLUE_4_BASE(logic_module);

    return 0;
  }

  int stop() override { return 0; }

  const char *name() const override { return "main_service_module"; }

  int tick() override {
    int ret = 0;
    rank_manager::me()->tick();
    rank_mirror_global::me()->tick();
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

  logic_config::me()->set_custom_config_loader([](atfw::atapp::app &app, logic_config &cfg) {
    auto config_ptr = atfw::util::memory::make_strong_rc<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>();
    app.parse_configures_into(*config_ptr, "ranksvr-ranking");
    cfg.mutable_custom_config() = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
  });

  logic_server_common_module_configure logic_mod_conf;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
