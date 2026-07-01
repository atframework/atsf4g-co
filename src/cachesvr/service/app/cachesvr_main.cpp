// Copyright 2026 atframework
// Created by owent

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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/handle_ss_rpc_cachesvrservice.atfw.gen.h"

#include "atframe/atapp_common_types.h"
#include "logic/cache_group_manager.h"

namespace {
class main_service_module : public atapp::module_impl, public std::enable_shared_from_this<main_service_module> {
 public:
  int init() override {
    {
      // register all router managers
    }

    INIT_CALL_FN(handle::cache::register_handles_for_cachesvrservice);

    if (get_app()->get_type_id() !=
        static_cast<atfw::atapp::app_id_t>(atfw::component::logic_service_type::kCacheSvr)) {
      FWLOGERROR("main_service_module only works for cache server, but current app type is {:#x}",
                 get_app()->get_type_id());
      return -1;
    }

    is_inited_ = true;
    return 0;
  }

  const char *name() const override { return "main_service_module"; }

  int tick() override {
    int ret = 0;
    if (!is_inited_) {
      return ret;
    }

    ret += cache_group_manager::me()->tick();

    return ret;
  }

 private:
  bool is_inited_ = false;
};

}  // namespace

int main(int argc, char *argv[]) {
  atapp::app app;

  // project directory
  {
    std::string proj_dir;
    util::file_system::dirname(__FILE__, 0, proj_dir, 5);
    util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  logic_server_common_module_configure logic_mod_conf;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  // run
  return app.run(uv_default_loop(), argc, const_cast<const char **>(argv), nullptr);
}
