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

#include <logic/logic_server_setup.h>
#include <logic/session_manager.h>
#include <router/router_manager_set.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "app/handle_cs_rpc_loginsvrclientservice.h"

#ifdef _MSC_VER

#  define INIT_CALL(MOD_NAME, ...)                                   \
    {                                                                \
      int res = MOD_NAME::me()->init(__VA_ARGS__);                   \
      if (res < 0) {                                                 \
        FWLOGERROR("initialize {} failed, res: {}", #MOD_NAME, res); \
        return res;                                                  \
      }                                                              \
    }

#  define INIT_CALL_FN(FUNC, ...)                                \
    {                                                            \
      int res = FUNC(__VA_ARGS__);                               \
      if (res < 0) {                                             \
        FWLOGERROR("initialize {} failed, res: {}", #FUNC, res); \
        return res;                                              \
      }                                                          \
    }

#  define RELOAD_CALL(RET_VAR, MOD_NAME, ...)                    \
    {                                                            \
      int res = MOD_NAME::me()->reload(__VA_ARGS__);             \
      if (res < 0) {                                             \
        FWLOGERROR("reload {} failed, res: {}", #MOD_NAME, res); \
        RET_VAR = res;                                           \
      }                                                          \
    }

#else
#  define INIT_CALL(MOD_NAME, args...)                               \
    {                                                                \
      int res = MOD_NAME::me()->init(args);                          \
      if (res < 0) {                                                 \
        FWLOGERROR("initialize {} failed, res: {}", #MOD_NAME, res); \
        return res;                                                  \
      }                                                              \
    }

#  define INIT_CALL_FN(FUNC, args...)                            \
    {                                                            \
      int res = FUNC(args);                                      \
      if (res < 0) {                                             \
        FWLOGERROR("initialize {} failed, res: {}", #FUNC, res); \
        return res;                                              \
      }                                                          \
    }

#  define RELOAD_CALL(RET_VAR, MOD_NAME, args...)                \
    {                                                            \
      int res = MOD_NAME::me()->reload(args);                    \
      if (res < 0) {                                             \
        FWLOGERROR("reload {} failed, res: {}", #MOD_NAME, res); \
        RET_VAR = res;                                           \
      }                                                          \
    }

#endif

struct app_handle_on_msg {
  app_handle_on_msg() {}

  int operator()(atapp::app &app, const atapp::app::message_sender_t &source, const atapp::app::message_t &msg) {
    if (0 == source.id) {
      FWLOGERROR("receive a message from unknown source or invalid body case");
      return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    }

    int ret = 0;
    switch (msg.type) {
      case ::atframe::component::service_type::EN_ATST_GATEWAY: {
        ret = cs_msg_dispatcher::me()->dispatch(source, msg);
        break;
      }

      case ::atframe::component::message_type::EN_ATST_SS_MSG: {
        ret = ss_msg_dispatcher::me()->dispatch(source, msg);
        break;
      }

      default: {
        FWLOGERROR("receive a message of invalid type: {}", msg.type);
        break;
      }
    }

    return ret;
  }
};

static int app_handle_on_forward_response(atapp::app &app, const atapp::app::message_sender_t &source,
                                          const atapp::app::message_t &msg, int32_t error_code) {
  if (error_code < 0) {
    FWLOGERROR("send data from {:#x}({}) to {:#x}({}) failed, sequence: {}, code: {}", app.get_id(), app.get_app_name(),
               source.id, source.name, msg.message_sequence, error_code);
  } else {
    FWLOGINFO("send data from {:#x}({}) to {:#x}({}) got response, sequence: {}, ", app.get_id(), app.get_app_name(),
              source.id, source.name, msg.message_sequence);
  }

  int ret = 0;
  switch (msg.type) {
    case ::atframe::component::message_type::EN_ATST_SS_MSG: {
      ret = ss_msg_dispatcher::me()->on_receive_send_data_response(source, msg, error_code);
      break;
    }

    default: {
      break;
    }
  }

  return ret;
}

static int app_handle_on_connected(atapp::app &, atbus::endpoint &ep, int status) {
  FWLOGINFO("app {:#x} connected, status: {}", ep.get_id(), status);
  return 0;
}

static int app_handle_on_disconnected(atapp::app &, atbus::endpoint &ep, int status) {
  FWLOGINFO("app {:#x} disconnected, status: {}", ep.get_id(), status);
  return 0;
}

class main_service_module : public atapp::module_impl {
 public:
  virtual int init() {
    FWLOGINFO("============ server initialize ============");
    INIT_CALL(logic_config, get_app()->get_id(), get_app()->get_app_name());
    INIT_CALL(excel::config_manager);

    // logic managers
    INIT_CALL(task_manager);
    INIT_CALL(session_manager);

    {
      INIT_CALL(router_manager_set);
      // register all router managers
      // router_player_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::loginsvrclientservice::register_handles_for_loginsvrclientservice);

    // reload will be triggered before init, so reload again here
    return excel::config_manager::me()->reload_all();
  }

  virtual int reload() {
    FWLOGINFO("============ server reload ============");
    int ret = 0;

    RELOAD_CALL(ret, logic_config, *get_app());
    ret = excel::config_manager::me()->reload_all();

    RELOAD_CALL(ret, task_manager);

    return ret;
  }

  virtual int stop() {
    FWLOGINFO("============ server stop ============");
    return 0;
  }

  virtual int timeout() {
    FWLOGINFO("============ server timeout ============");
    return 0;
  }

  virtual const char *name() const { return "main_service_module"; }

  virtual int tick() {
    int ret = 0;
    ret += session_manager::me()->proc();
    ret += task_manager::me()->tick(util::time::time_utility::get_sys_now(),
                                    static_cast<int>(1000 * util::time::time_utility::get_now_usec()));
    ret += router_manager_set::me()->tick();

    return ret;
  }
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
  logic_server_common_module_conf_t logic_mod_conf;
  logic_mod_conf.enable_watch_battlesvr = false;
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(std::make_shared<main_service_module>());
  app.add_module(cs_msg_dispatcher::me());
  app.add_module(ss_msg_dispatcher::me());
  app.add_module(db_msg_dispatcher::me());

  // setup message handle
  app.set_evt_on_forward_request(app_handle_on_msg());
  app.set_evt_on_forward_response(app_handle_on_forward_response);
  app.set_evt_on_app_connected(app_handle_on_connected);
  app.set_evt_on_app_disconnected(app_handle_on_disconnected);

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
