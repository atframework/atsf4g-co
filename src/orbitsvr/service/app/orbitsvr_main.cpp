// Copyright 2026 atframework
// Created by yousongyang

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <libatbus_protocol.h>
#include <time/time_utility.h>

#include <memory/object_allocator.h>

#include <cstdlib>
#include <sstream>
#include <vector>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_no_req_base.h>
#include <dispatcher/task_action_ss_req_base.h>

#include <config/server_frame_build_feature.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>
#include <logic/orbit_msg_dispatcher.h>
#include <logic/orbit_server_manager.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/orbit_client_rpc/orbitclientrpcservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/config/orbitsvr_config.pb.h>
#include <protocol/pbdesc/orbit_service.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "app/handle_orbit_rpc_orbitserverrpcservice.atfw.gen.h"
#include "app/handle_ss_rpc_dtmqproxysvrnotifyservice.atfw.gen.h"
#include "app/handle_ss_rpc_orbitsvrservice.atfw.gen.h"

#include <logic/room/orbit_room_manager.h>

namespace {
uint64_t make_orbit_server_unique_id() {
  return static_cast<uint64_t>((atfw::util::time::time_utility::get_sys_now() -
                                PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
                               << 23) +
         static_cast<uint64_t>(atfw::util::time::time_utility::get_now_usec() << 3) +
         static_cast<uint64_t>(logic_config::me()->get_local_server_id());
}

void add_command_response(atfw::util::cli::callback_param params, const std::string &message) {
  ::atframework::atapp::app::add_custom_command_rsp(params, message);
}

}  // namespace

class main_service_module : public atfw::atapp::module_impl {
 public:
  int init() override {
    // register handles
    INIT_CALL_FN(handle::orbit_server_rpc::register_handles_for_orbitserverrpcservice);
    INIT_CALL_FN(handle::orbit::register_handles_for_orbitsvrservice);
    INIT_CALL_FN(handle::dtmq::register_handles_for_dtmqproxysvrnotifyservice);
    INIT_CALL(orbit_room_manager);
    const auto &server_cfg =
        logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::orbitsvr_cfg>();
    int orbit_init_result =
        orbit_server_manager::me()->init(make_orbit_server_unique_id(), server_cfg.server_heartbeat_interval_sec());
    if (orbit_init_result < 0) {
      return orbit_init_result;
    }

    orbit_server_manager::me()->set_on_client_start_notify([](rpc::context &ctx, const std::string &client_id,
                                                              const std::string &client_addr,
                                                              const std::string &payload) -> rpc::result_code_type {
      FWLOGINFO("orbit client {} is ready from {}, startup payload size: {}", client_id, client_addr, payload.size());
      RPC_RETURN_CODE(
          RPC_AWAIT_CODE_RESULT(orbit_room_manager::me()->on_client_start(ctx, client_id, client_addr, payload)));
    });
    orbit_server_manager::me()->set_on_client_end_notify(
        [](rpc::context &ctx, const std::string &client_id, orbit::EnClientExitReason exit_reason,
           const std::string &payload, int32_t exit_code) -> rpc::result_code_type {
          FWLOGINFO("orbit client {} exited, reason: {}, code: {}, payload size: {}", client_id,
                    static_cast<int>(exit_reason), exit_code, payload.size());
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
              orbit_room_manager::me()->on_client_end(ctx, client_id, payload, exit_reason, exit_code)));
        });

    return 0;
  }

  int stop() override {
    orbit_room_manager::me()->stop();
    orbit_server_manager::me()->stop();
    return 0;
  }

  int tick() override {
    int ret = 0;
    ret += rpc::dtmq::client_subscriber::global_tick(logic_server_get_current_tick_context());
    orbit_room_manager::me()->tick();
    orbit_server_manager::me()->tick();
    return ret;
  }

  static int cmd_start_client(atfw::util::cli::callback_param params) {
    if (params.get_params_number() < 2) {
      add_command_response(params, "usage: orbit-start-client <region> <client-id> [match-tag]");
      return 0;
    }

    std::string region = params[0]->to_cpp_string();
    std::string client_id = params[1]->to_cpp_string();
    std::string match_tag;
    if (params.get_params_number() > 2) {
      match_tag = params[2]->to_cpp_string();
    }

    rpc::context ctx{rpc::context::create_without_task()};
    auto invoke_result = rpc::async_invoke(
        ctx, "lobbysvr.orbit_start_client",
        [region = std::move(region), client_id = std::move(client_id),
         match_tag = std::move(match_tag)](rpc::context &child_ctx) -> rpc::result_code_type {
          orbit::DAgentClientStartArgs request;
          orbit_room_manager::fill_client_start_args_from_template_id(1, client_id, request);
          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(orbit_server_manager::me()->start_client(child_ctx, region, request)));
        });

    if (invoke_result.is_error()) {
      add_command_response(params, "orbit-start-client failed to schedule async task");
      return 0;
    }

    add_command_response(params, std::string{"orbit-start-client scheduled for client "} + params[1]->to_cpp_string());
    return 0;
  }

  const char *name() const override { return "main_service_module"; }
};

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // Common logic
  logic_server_common_module_configure logic_mod_conf;

  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app &app_, logic_config & /*cfg*/, logic_config::server_instance_config_ptr &to_) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::config::orbitsvr_cfg>();
        app_.parse_configures_into(*config_ptr, "orbitsvr", "ATAPP_ORBITSVR");
        to_ = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });

  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(ss_msg_dispatcher::me());
  app.add_module(orbit_msg_dispatcher::me());
  app.add_module(atfw::memory::stl::make_shared<main_service_module>());

  app.get_command_manager()
      ->bind_cmd("orbit-start-client", &main_service_module::cmd_start_client)
      ->set_help_msg("orbit-start-client <region> <client-id> [match-tag]    launch an Orbit client");

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
