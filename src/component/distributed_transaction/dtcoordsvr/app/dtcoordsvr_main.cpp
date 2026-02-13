
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

#include <memory/object_allocator.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <config/server_frame_build_feature.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>

#include <config/server_frame_build_feature.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/dtcoordsvr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "handle_ss_rpc_dtcoordsvrservice.h"

#include <logic/transaction_manager.h>

namespace {
static std::shared_ptr<hello::config::dtcoordsvr_cfg> &get_server_cfg_pointer() {
  static std::shared_ptr<hello::config::dtcoordsvr_cfg> cfg = std::make_shared<hello::config::dtcoordsvr_cfg>();
  return cfg;
}
}  // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage)
const hello::config::dtcoordsvr_cfg &get_dtcoordsvr_cfg() { return *get_server_cfg_pointer(); }

class main_service_module : public atfw::atapp::module_impl {
 public:
  int reload() override {
    std::shared_ptr<hello::config::dtcoordsvr_cfg> cfg = std::make_shared<hello::config::dtcoordsvr_cfg>();
    get_app()->parse_configures_into(*cfg, "dtcoordsvr", "ATAPP_DTCOORDSVR");
    get_server_cfg_pointer().swap(cfg);
    return 0;
  }

  int init() override {
    {
      // register all router managers
    }

    // register handles
    INIT_CALL_FN(handle::transaction::register_handles_for_dtcoordsvrservice);
    return 0;
  };

  const char *name() const override { return "main_service_module"; }

  int tick() override {
    int ret = 0;
    ret += transaction_manager::me()->tick();
    return ret;
  }

  int stop() override {
    transaction_manager::me()->stop();
    return 0;
  }
};

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  logic_server_common_module_configure logic_mod_conf;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(atfw::memory::stl::make_shared<main_service_module>());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
