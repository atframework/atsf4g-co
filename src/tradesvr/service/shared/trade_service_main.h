// Copyright 2026 atframework

#pragma once

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <logic/logic_server_setup.h>

#include <string>

namespace trade_service {
template <class MainModule>
int run(int argc, char* argv[], const char* source_file, logic_config::server_instance_config_loader_func load_config) {
  atfw::atapp::app app;

  std::string project_directory;
  atfw::util::file_system::dirname(source_file, 0, project_directory, 4);
  atfw::util::log::log_formatter::set_project_directory(project_directory.c_str(), project_directory.size());
  logic_config::me()->set_server_instance_config_loader(load_config);

  const logic_server_common_module_configure common_config;
  int result = logic_server_setup_common(app, common_config);
  if (result < 0) {
    return result;
  }

  app.add_module(atfw::memory::stl::make_shared<MainModule>());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());
  return app.run(uv_default_loop(), argc, const_cast<const char**>(argv), nullptr);
}
}  // namespace trade_service
