// Copyright 2026 atframework
// Created by atsf4g-co orbit_room module migration

#include "logic/room/orbit_room_manager.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/excel/config_easy_api.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <logic/orbit_server_manager.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/config/com.struct.orbit.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

int orbit_room_manager::init() {
  is_inited_ = true;
  return 0;
}

int orbit_room_manager::reload() { return 0; }

int orbit_room_manager::tick() {
  if (!is_inited_) {
    return 0;
  }

  for (auto iter = room_index_by_client_id_.begin(); iter != room_index_by_client_id_.end();) {
    iter->second->tick();
    if (iter->second->ready_to_destroy()) {
      iter->second->on_destroy();
      iter = room_index_by_client_id_.erase(iter);
    } else {
      ++iter;
    }
  }
  return 0;
}

int orbit_room_manager::stop() {
  if (!is_inited_) {
    return 0;
  }
  is_closing_ = true;
  return 0;
}

atfw::util::memory::strong_rc_ptr<orbit_room> orbit_room_manager::get_room(const std::string& client_id) noexcept {
  auto iter = room_index_by_client_id_.find(client_id);
  if (iter == room_index_by_client_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

rpc::result_code_type orbit_room_manager::create_room(rpc::context& ctx,
                                                      const PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq& req,
                                                      uint64_t match_server_id) {
  if (is_closing_) {
    FWLOGERROR("orbit_room_manager create_room failed, is_closing_ is true");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }

  const std::string& client_id = req.room_key().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit_room_manager create_room failed, client_id is empty");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (room_index_by_client_id_.find(client_id) != room_index_by_client_id_.end()) {
    FWLOGERROR("orbit_room_manager create_room failed, client_id {} already exists", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_ALREADY_EXISTS);
  }
  if (req.room_data().client_template_id() == 0 || req.room_data().region().empty()) {
    FWLOGERROR("orbit_room_manager create_room failed, client_template_id or region is empty");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (req.room_data().match_id().empty()) {
    FWLOGERROR("orbit_room_manager create_room failed, match_id is empty");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  atfw::orbit::DAgentClientStartArgs args;
  if (!fill_client_start_args_from_template_id(req.room_data().client_template_id(), client_id, args)) {
    FWLOGERROR("orbit_room_manager create_room failed, fill_client_start_args_from_template_id failed");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_CLIENT_TEMPLATE_NOT_FOUND);
  }

  auto room = atfw::util::memory::make_strong_rc<orbit_room>(req.room_key(), req.room_data());
  int32_t ret = room->create(ctx, match_server_id);
  if (ret != 0) {
    FWLOGERROR("orbit_room_manager create_room failed, client_id: {}, ret: {}", client_id, ret);
    RPC_RETURN_CODE(ret);
  }
  room_index_by_client_id_[client_id] = room;
  ret = RPC_AWAIT_CODE_RESULT(room->start_client(ctx, args));
  if (ret != 0) {
    FWLOGERROR("orbit_room {} start_client failed, ret: {}", room->get_client_id(), ret);
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int32_t orbit_room_manager::init_user(EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                      const PROJECT_NAMESPACE_ID::SSOrbitUserInitReq& req) {
  const std::string& client_id = req.room_key().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit_room_manager init_user failed, client_id is empty");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  auto room = get_room(client_id);
  if (!room) {
    FWLOGERROR("orbit_room_manager init_user failed, room {} not found", client_id);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  return room->init_user(req.user_list(), req.is_last_one());
}

rpc::result_code_type orbit_room_manager::on_client_start(rpc::context& ctx, const std::string& client_id,
                                                          const std::string& client_addr, const std::string& payload) {
  FWLOGINFO("orbit_room_manager on_client_start, client_id: {}, addr: {}, payload size: {}", client_id, client_addr,
            payload.size());
  auto room = get_room(client_id);
  if (!room) {
    FWLOGERROR("orbit_room_manager on_client_start, room {} not found", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  RPC_RETURN_CODE(room->on_client_start(ctx, client_addr));
}

rpc::result_code_type orbit_room_manager::on_client_end(rpc::context& ctx, const std::string& client_id,
                                                        const std::string&, atfw::orbit::EnClientExitReason exit_reason,
                                                        int32_t exit_code) {
  FWLOGINFO("orbit_room_manager on_client_end, client_id: {}", client_id);
  auto room = get_room(client_id);
  if (!room) {
    FWLOGWARNING("orbit_room_manager on_client_end, room {} not found", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  RPC_RETURN_CODE(room->on_client_end(ctx, exit_reason, exit_code));
}

rpc::result_code_type orbit_room_manager::on_user_finish(
    rpc::context& ctx, const std::string& client_id,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results) {
  FWLOGINFO("orbit_room_manager on_user_finish, client_id: {}, result size: {}", client_id, results.size());
  auto room = get_room(client_id);
  if (!room) {
    FWLOGWARNING("orbit_room_manager on_user_finish, room {} not found", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  RPC_RETURN_CODE(room->on_user_finish(ctx, results));
}

bool orbit_room_manager::fill_client_start_args_from_template_id(uint32_t template_id, const std::string& client_id,
                                                                 atfw::orbit::DAgentClientStartArgs& args) {
  args.mutable_client_start_args()->mutable_client_id()->set_client_id(client_id);
  auto row = excel::get_ExcelOrbitClientTemplate_by_client_template_id(template_id);
  if (row == nullptr) {
    return false;
  }
  args.mutable_resource()->set_normal_cpu(row->expected_normal_cpu());
  args.mutable_resource()->set_normal_memory_mb(row->expected_normal_memory_mb());
  args.mutable_resource()->set_seed_cpu(row->expected_seed_cpu());
  args.mutable_resource()->set_seed_memory_mb(row->expected_seed_memory_mb());
  args.set_startup_timeout_sec(row->startup_timeout_sec());
  args.set_heartbeat_timeout_sec(row->heartbeat_timeout_sec());
  *args.mutable_client_start_args()->mutable_custom_args() = row->launch_args();
  args.set_match_tag(row->match_tag());
  return true;
}
