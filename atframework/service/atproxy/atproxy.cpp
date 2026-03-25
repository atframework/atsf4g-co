// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include <uv.h>

#include <opentelemetry/semconv/incubating/deployment_attributes.h>

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <libatbus_protocol.h>
#include <time/time_utility.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <vector>

#include "atproxy_manager.h"

namespace {
static int app_handle_on_response(atfw::atapp::app &app, const atfw::atapp::app::message_sender_t &source,
                                  const atfw::atapp::app::message_t &msg, int32_t error_code) {
  if (error_code < 0) {
    FWLOGERROR("send data from {:#x} to {:#x} failed, sequence: {}, code: {}", app.get_id(), source.id,
               msg.message_sequence, error_code);
  } else {
    FWLOGDEBUG("send data from {:#x} to {:#x} finished, sequence: {}", app.get_id(), source.id, msg.message_sequence);
  }
  return 0;
}
}  // namespace

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  std::shared_ptr<atframework::proxy::atproxy_manager> proxy_mgr_mod =
      std::make_shared<atframework::proxy::atproxy_manager>();
  if (!proxy_mgr_mod) {
    fprintf(stderr, "create atproxy manager module failed\n");
    return -1;
  }

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // setup command
  atfw::util::cli::cmd_option_ci::ptr_type cmgr = app.get_command_manager();
  cmgr->bind_cmd("-env",
                 [&app](util::cli::callback_param params) {
                   if (params.get_params_number() <= 0) {
                     return;
                   }

                   if (params[0]->to_cpp_string().empty()) {
                     return;
                   }

                   app.set_metadata_label(opentelemetry::semconv::deployment::kDeploymentEnvironmentName,
                                          params[0]->to_cpp_string());
                 })
      ->set_help_msg("-env [text]                                               set a env name.");
  cmgr->bind_cmd("show-configure",
                 [&app, proxy_mgr_mod](util::cli::callback_param params) {
                   std::string app_configure =
                       std::string("atapp configure:\n") + app.get_origin_configure().Utf8DebugString();
                   ::atfw::atapp::app::add_custom_command_rsp(params, app_configure);
                   ::atfw::atapp::app::add_custom_command_rsp(
                       params,
                       std::string("atproxy configure:\n") + proxy_mgr_mod->get_origin_conf().Utf8DebugString());
                 })
      ->set_help_msg("show-configure                                            show service configure");

  // setup module
  app.add_module(proxy_mgr_mod);

  // setup message handle
  app.set_evt_on_forward_response(app_handle_on_response);

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
