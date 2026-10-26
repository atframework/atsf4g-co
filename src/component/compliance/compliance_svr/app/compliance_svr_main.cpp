// Copyright 2026 atframework

#include <common/file_system.h>
#include <log/log_wrapper.h>
#include <memory/object_allocator.h>

#include <atframe/atapp.h>

#include <config/logic_config.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/compliance_svr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory>
#include <string>

#include "app/handle_ss_rpc_complianceservice.atfw.gen.h"

namespace {
class main_service_module : public atfw::atapp::module_impl {
 public:
  int init() override {
    INIT_CALL_FN(handle::compliance::register_handles_for_complianceservice);
    return 0;
  }

  const char* name() const override { return "main_service_module"; }
};
}  // namespace

int main(int argc, char* argv[]) {
  atfw::atapp::app app;

  {
    std::string project_directory;
    atfw::util::file_system::dirname(__FILE__, 0, project_directory, 4);
    atfw::util::log::log_formatter::set_project_directory(project_directory.c_str(), project_directory.size());
  }

  logic_config::me()->set_server_instance_config_loader([](atfw::atapp::app& current_app, logic_config&,
                                                           logic_config::server_instance_config_ptr& output) {
    auto config = atfw::component::memory::stl::make_strong_rc<atframework::compliance::config::compliance_svr_cfg>();
    current_app.parse_configures_into(*config, "compliance_svr", "ATAPP_COMPLIANCE_SVR");
    output = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config);
  });

  logic_server_common_module_configure module_config;
  if (logic_server_setup_common(app, module_config) < 0) {
    return -1;
  }

  app.add_module(std::make_shared<main_service_module>());
  app.add_module(ss_msg_dispatcher::me());

  return app.run(uv_default_loop(), argc, const_cast<const char**>(argv), nullptr);
}
