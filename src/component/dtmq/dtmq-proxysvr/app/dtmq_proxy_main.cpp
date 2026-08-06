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

#include <protocol/config/dtmq_proxy.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/handle_ss_rpc_dtmqproxysvrnotifyservice.atfw.gen.h"
#include "app/handle_ss_rpc_dtmqproxysvrservice.atfw.gen.h"
#include "logic/mq_channel_manager.h"

namespace {
class main_service_module : public atfw::atapp::module_impl {
 public:
  int init() override {
    {
      // register all router managers
    }

    // register handles
    INIT_CALL_FN(handle::dtmq::register_handles_for_dtmqproxysvrservice);
    INIT_CALL_FN(handle::dtmq::register_handles_for_dtmqproxysvrnotifyservice);
    INIT_CALL(mq_channel_manager);

    // 设置依赖，阻止数据库和ss通信模块在 mq_channel_manager 前退出
    auto suspend_stop_callback = []() -> bool {
      if (mq_channel_manager::is_instance_destroyed()) {
        return false;
      }

      if (!mq_channel_manager::me()->is_can_stopped()) {
        return true;
      }

      return false;
    };
    auto suspend_timeout = protobuf_to_chrono_duration<std::chrono::system_clock::duration>(
        get_app()->get_origin_configure().timer().stop_timeout());

    auto *common_mod = logic_server_last_common_module();
    if (nullptr != common_mod) {
      common_mod->suspend_stop(suspend_timeout, suspend_stop_callback);

      // TODO(owent): 等接入 prestop 流程后增加这个,prestop流程应该由 schedule-hpa-node-shutdown 指令联动触发
      // common_mod->add_prestop_callback(
      //     []() -> int32_t {
      //       atapp::app *current_app = atapp::app::get_last_instance();
      //       if (current_app != nullptr) {
      //         mq_channel_manager::me()->pre_stoping();
      //         return 0;
      //       }
      //       FWLOGERROR("can not find logic_server_common_module when pre_stop");
      //       return -1;
      //     },
      //     "stop_all_channel");
    }

    return 0;
  };

  const char *name() const override { return "main_service_module"; }

  int tick() override { return mq_channel_manager::me()->tick(); }

  int stop() override { return mq_channel_manager::me()->stop(); }

  void ready() override {}

  int reload() override {
    int ret = 0;
    if (!is_actived()) {
      // app::init时会调用这个接口,此时某些模块没有加载表是不能reload的,挡住init时的调用
      return ret;
    }

    // 只有在通过CMD RELOAD时才会调用到此处
    FWLOGINFO("MAIN SERVER MODULE RELOAD");

    RELOAD_CALL(ret, mq_channel_manager);
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
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::dtmq::config::dtmq_proxysvr_cfg>();
        app_.parse_configures_into(*config_ptr, "dtmq_proxysvr", "ATAPP_DTMQ_PROXYSVR");
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
