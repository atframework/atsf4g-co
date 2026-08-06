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

#include <memory/object_allocator.h>

#include <service_discovery_index/discovery_index.h>

#include <memory>

#include "app/handle_cs_rpc_authsvrclientservice.atfw.gen.h"
#include "authsvr_helper.h"

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
      // router_player_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::authsvrclientservice::register_handles_for_authsvrclientservice);

    discovery_index_ = atfw::component::service_discovery_index::create(get_app()->get_service_discovery_module());
    discovery_index_->initialize();

    return 0;
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

  const atfw::component::service_discovery_index::ptr_t &get_discovery_index() const noexcept {
    return discovery_index_;
  }

 private:
  atfw::component::service_discovery_index::ptr_t discovery_index_;
};
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

  app.add_module(cs_msg_dispatcher::me());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  app.add_module(std::make_shared<main_service_module>());

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
