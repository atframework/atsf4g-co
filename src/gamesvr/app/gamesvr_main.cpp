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

#include <config/excel/config_manager.h>
#include <config/excel_config_wrapper.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <logic/logic_server_setup.h>
#include <logic/session_manager.h>
#include <router/router_manager_set.h>
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
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
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
 private:
  static router_player_cache::object_ptr_t create_player_fn(uint64_t user_id, uint32_t zone_id,
                                                            const std::string &openid) {
    return std::static_pointer_cast<player_cache>(player::create(user_id, zone_id, openid));
  }

 public:
  virtual int init() {
    stop_log_tm_ = 0;
    FWLOGINFO("============ server initialize ============");
    INIT_CALL(logic_config, get_app()->get_id(), get_app()->get_app_name());
    INIT_CALL(excel::config_manager);

    // logic managers
    INIT_CALL(task_manager);
    INIT_CALL(session_manager);

    {
      INIT_CALL(router_manager_set);
      // register all router managers
      router_player_manager::me();
    }

    // register handles
    INIT_CALL_FN(handle::game::register_handles_for_gamesvrservice);
    INIT_CALL_FN(handle::gamesvrclientservice::register_handles_for_gamesvrclientservice);

    // reload will be triggered before init, so reload again here

    // setup how to create player
    router_player_manager::me()->set_create_object_fn(create_player_fn);

    return excel_config_wrapper_reload_all(true);
  }

  virtual int reload() {
    FWLOGINFO("============ server reload ============");
    int ret = 0;

    RELOAD_CALL(ret, logic_config, *get_app());
    {
      int res = excel_config_wrapper_reload_all(false);
      if (res < 0) {
        WLOGERROR("reload excel config failed, res: %d", res);
        ret = res;
      }
    }

    RELOAD_CALL(ret, task_manager);

    return ret;
  }

  virtual int stop() {
    time_t now = util::time::time_utility::get_sys_now();
    if (now != stop_log_tm_) {
      stop_log_tm_ = now;
      FWLOGINFO("============ server stop ============");
    }
    int ret = router_manager_set::me()->stop();
    if (ret < 0) {
      FWLOGERROR("router_manager_set stop failed, res: {}", ret);
      return ret;
    }

    // 保存任务未完成，需要继续等待
    if (!router_manager_set::me()->is_closed()) {
      ret = 1;
    }

    return ret;
  }

  virtual int timeout() {
    FWLOGINFO("============ server timeout ============");
    router_manager_set::me()->force_close();
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

 private:
  time_t stop_log_tm_;
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
