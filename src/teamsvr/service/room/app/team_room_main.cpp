// Copyright 2026 atframework

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

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/team_room.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_context.h>

#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/handle_ss_rpc_dtmqproxysvrnotifyservice.atfw.gen.h"
#include "app/handle_ss_rpc_teamroomservice.atfw.gen.h"
#include "logic/room/team_room_manager.h"

namespace {
class main_service_module : public atfw::atapp::module_impl {
 public:
  int init() override {
    {
      // register all router managers
    }

    // register handles
    INIT_CALL_FN(handle::team::register_handles_for_teamroomservice);
    INIT_CALL_FN(handle::dtmq::register_handles_for_dtmqproxysvrnotifyservice);
    return team_room_manager::me()->init();
  };

  const char *name() const override { return "main_service_module"; }

  int tick() override {
    int ret = 0;
    // dtmq 订阅者心跳调度
    ret += rpc::dtmq::client_subscriber::global_tick(logic_server_get_current_tick_context());
    // 房间维护: 乐观锁续租、定期压缩、过期数据清理
    ret += team_room_manager::me()->tick(logic_server_get_current_tick_context());
    return ret;
  }

  int stop() override {
    team_room_manager::me()->clear();
    return 0;
  }

  void ready() override {}

  int reload() override {
    int ret = 0;
    if (!is_actived()) {
      // app::init时会调用这个接口,此时某些模块没有加载表是不能reload的,挡住init时的调用
      return ret;
    }

    // 只有在通过CMD RELOAD时才会调用到此处
    FWLOGINFO("MAIN SERVER MODULE RELOAD");

    return ret;
  }
};
}  // namespace

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app &app_, logic_config & /*cfg*/, logic_config::server_instance_config_ptr &to) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::config::teamsvr_room_cfg>();
        app_.parse_configures_into(*config_ptr, "teamsvr_room", "ATAPP_TEAMSVR_ROOM");
        to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });

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
