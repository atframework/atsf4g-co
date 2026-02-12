// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include <uv.h>

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

  // setup module
  app.add_module(proxy_mgr_mod);

  // setup message handle
  app.set_evt_on_forward_response(app_handle_on_response);

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
