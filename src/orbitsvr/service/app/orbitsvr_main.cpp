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
#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_no_req_base.h>
#include <dispatcher/task_action_ss_req_base.h>

#include <config/server_frame_build_feature.h>

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>
#include <logic/orbit_msg_dispatcher.h>
#include <logic/orbit_server_manager.h>
#include <rpc/orbit_client_rpc/orbitclientrpcservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/orbit_service.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "app/handle_orbit_rpc_orbitserverrpcservice.atfw.gen.h"
#include "app/handle_ss_rpc_orbitsvrservice.atfw.gen.h"

#include <logic/room/orbit_room_manager.h>

namespace {

constexpr uint64_t kOrbitServerHeartbeatIntervalSec = 5;
constexpr double kOrbitClientExpectedNormalCpu = 0.1;
constexpr double kOrbitClientExpectedNormalMemoryMb = 64.0;
constexpr double kOrbitClientExpectedSeedCpu = 0.05;
constexpr double kOrbitClientExpectedSeedMemoryMb = 32.0;
constexpr uint32_t kOrbitClientStartupTimeoutSec = 90;
constexpr uint32_t kOrbitClientHeartbeatTimeoutSec = 15;
constexpr const char *kOrbitWelcomePayload = "orbit:welcome";

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
    INIT_CALL(orbit_room_manager);
    int orbit_init_result =
        orbit_server_manager::me()->init(make_orbit_server_unique_id(), kOrbitServerHeartbeatIntervalSec);
    if (orbit_init_result < 0) {
      return orbit_init_result;
    }

    orbit_server_manager::me()->set_on_client_start_notify([](rpc::context &, const std::string &client_id,
                                                              const std::string &client_addr,
                                                              const std::string &payload) -> rpc::result_code_type {
      FWLOGINFO("orbit client {} is ready from {}, startup payload size: {}", client_id, client_addr, payload.size());
      orbit_room_manager::me()->on_client_start(client_id, client_addr, payload);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    });
    orbit_server_manager::me()->set_on_client_end_notify(
        [](rpc::context &, const std::string &client_id, orbit::EnClientExitReason exit_reason,
           const std::string &payload, int32_t exit_code) -> rpc::result_code_type {
          FWLOGINFO("orbit client {} exited, reason: {}, code: {}, payload size: {}", client_id,
                    static_cast<int>(exit_reason), exit_code, payload.size());
          orbit_room_manager::me()->on_client_end(client_id, payload);
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        });

    return 0;
  }

  int stop() override {
    orbit_room_manager::me()->stop();
    orbit_server_manager::me()->stop();
    return 0;
  }

  int tick() override {
    orbit_room_manager::me()->tick();
    orbit_server_manager::me()->tick();
    return 0;
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
          request.mutable_client_start_args()->mutable_client_id()->set_client_id(client_id);
          request.mutable_resource()->set_normal_cpu(kOrbitClientExpectedNormalCpu);
          request.mutable_resource()->set_normal_memory_mb(kOrbitClientExpectedNormalMemoryMb);
          request.mutable_resource()->set_seed_cpu(kOrbitClientExpectedSeedCpu);
          request.mutable_resource()->set_seed_memory_mb(kOrbitClientExpectedSeedMemoryMb);
          request.set_startup_timeout_sec(kOrbitClientStartupTimeoutSec);
          request.set_heartbeat_timeout_sec(kOrbitClientHeartbeatTimeoutSec);

          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(orbit_server_manager::me()->start_client(child_ctx, region, request, match_tag)));
        });

    if (invoke_result.is_error()) {
      add_command_response(params, "orbit-start-client failed to schedule async task");
      return 0;
    }

    add_command_response(params, std::string{"orbit-start-client scheduled for client "} + params[1]->to_cpp_string());
    return 0;
  }

  // ---- 步骤 6 冒烟辅助命令（模拟 matchsvr/gamesvr/Client 调用，验证核心流程） ----
  static int cmd_create_room(atfw::util::cli::callback_param params) {
    if (params.get_params_number() < 2) {
      add_command_response(params, "usage: orbit-create-room <client-id> [map-id] [region]");
      return 0;
    }

    std::string client_id = params[0]->to_cpp_string();
    int32_t map_id = 0;
    std::string region;
    if (params.get_params_number() > 1) {
      map_id = static_cast<int32_t>(strtol(params[1]->to_cpp_string().c_str(), nullptr, 10));
    }
    if (params.get_params_number() > 2) {
      region = params[2]->to_cpp_string();
    }

    PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq req;
    req.mutable_room_key()->set_client_id(client_id);
    req.mutable_room_data()->mutable_map_data()->set_map_id(map_id);
    req.mutable_room_data()->mutable_map_data()->set_region(region);

    PROJECT_NAMESPACE_ID::SSOrbitCreateRoomRsp rsp;
    rpc::context ctx{rpc::context::create_without_task()};
    int32_t ret = orbit_room_manager::me()->create_room(ctx, req, rsp);
    add_command_response(params, atfw::util::log::format("orbit-create-room {} -> {}", client_id, ret));
    return 0;
  }

  static int cmd_join_room(atfw::util::cli::callback_param params) {
    if (params.get_params_number() < 3) {
      add_command_response(params, "usage: orbit-join-room <user-id> <zone-id>");
      return 0;
    }

    uint64_t user_id = strtoull(params[0]->to_cpp_string().c_str(), nullptr, 10);
    uint32_t zone_id = static_cast<uint32_t>(strtoul(params[1]->to_cpp_string().c_str(), nullptr, 10));

    PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomReq req;
    PROJECT_NAMESPACE_ID::DOrbitUserInitData* user_data = req.add_user_init_datas();
    if (nullptr != user_data) {
      user_data->mutable_user_key()->mutable_user_key()->set_user_id(user_id);
      user_data->mutable_user_key()->mutable_user_key()->set_zone_id(zone_id);
    }

    PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomRsp rsp;
    rpc::context ctx{rpc::context::create_without_task()};
    int32_t ret = orbit_room_manager::me()->join_room(ctx, req, rsp);
    add_command_response(params, atfw::util::log::format("orbit-join-room user {},{} -> {}", user_id, zone_id, ret));
    return 0;
  }

  static int cmd_user_finish(atfw::util::cli::callback_param params) {
    if (params.get_params_number() < 4) {
      add_command_response(params, "usage: orbit-user-finish <client-id> <user-id> <zone-id>");
      return 0;
    }

    std::string client_id = params[0]->to_cpp_string();
    uint64_t user_id = strtoull(params[1]->to_cpp_string().c_str(), nullptr, 10);
    uint32_t zone_id = static_cast<uint32_t>(strtoul(params[2]->to_cpp_string().c_str(), nullptr, 10));

    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult> results;
    PROJECT_NAMESPACE_ID::DOrbitUserFinishResult* result = results.Add();
    if (nullptr != result) {
      result->mutable_user_key()->mutable_user_key()->set_user_id(user_id);
      result->mutable_user_key()->mutable_user_key()->set_zone_id(zone_id);
      result->set_index(0);
    }

    orbit_room_manager::me()->on_user_finish(client_id, results);
    add_command_response(params, atfw::util::log::format("orbit-user-finish user {},{} for client {} scheduled", user_id,
                                                         zone_id, client_id));
    return 0;
  }

  static int cmd_list_rooms(atfw::util::cli::callback_param params) {
    add_command_response(params,
                         atfw::util::log::format("orbit rooms count: {}", orbit_room_manager::me()->get_room_size()));
    return 0;
  }

  static int cmd_get_room(atfw::util::cli::callback_param params) {
    if (params.get_params_number() < 1) {
      add_command_response(params, "usage: orbit-get-room <client-id>");
      return 0;
    }

    std::string client_id = params[0]->to_cpp_string();
    std::shared_ptr<orbit_room> room = orbit_room_manager::me()->get_room(client_id);
    if (!room) {
      add_command_response(params, atfw::util::log::format("orbit-get-room {} not found", client_id));
      return 0;
    }

    PROJECT_NAMESPACE_ID::DOrbitRoomSnapshotData snap;
    room->dump(snap);
    const PROJECT_NAMESPACE_ID::DOrbitRoomRunningData& running = snap.running_data();
    add_command_response(params,
                         atfw::util::log::format("orbit-get-room {} status: {}, create_timepoint: {}, client_addr: {}",
                                                 client_id, static_cast<int32_t>(running.room_status()),
                                                 running.create_timepoint(), running.client_address()));
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
  if (logic_server_setup_common(app, logic_mod_conf) < 0) {
    return -1;
  }

  app.add_module(ss_msg_dispatcher::me());
  app.add_module(orbit_msg_dispatcher::me());
  app.add_module(atfw::memory::stl::make_shared<main_service_module>());

  app.get_command_manager()
      ->bind_cmd("orbit-start-client", &main_service_module::cmd_start_client)
      ->set_help_msg("orbit-start-client <region> <client-id> [match-tag]    launch an Orbit client");

  app.get_command_manager()
      ->bind_cmd("orbit-create-room", &main_service_module::cmd_create_room)
      ->set_help_msg("orbit-create-room <client-id> [map-id] [region]    simulate matchsvr create_room");
  app.get_command_manager()
      ->bind_cmd("orbit-join-room", &main_service_module::cmd_join_room)
      ->set_help_msg("orbit-join-room <user-id> <zone-id>    simulate gamesvr join_room");
  app.get_command_manager()
      ->bind_cmd("orbit-user-finish", &main_service_module::cmd_user_finish)
      ->set_help_msg("orbit-user-finish <client-id> <user-id> <zone-id>    simulate Client user_finish");
  app.get_command_manager()
      ->bind_cmd("orbit-list-rooms", &main_service_module::cmd_list_rooms)
      ->set_help_msg("orbit-list-rooms    list orbit room count");
  app.get_command_manager()
      ->bind_cmd("orbit-get-room", &main_service_module::cmd_get_room)
      ->set_help_msg("orbit-get-room <client-id>    dump orbit room snapshot");

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
