// Copyright 2026 atframework
// Created by atsf4g-co battle module migration

#include "logic/room/orbit_room_manager.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <logic/orbit_server_manager.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

namespace {
// ===== 用户待办（TODO-USER）：Orbit Client 启动资源/超时接入配置 =====
// 现状：CPU/内存/启动超时/心跳超时用常量占位。
// 需要你决定是否接入配置（logic_config / excel）；若接入，告诉我字段名，
// 我把 kOrbitClientExpected* / kOrbitClient*TimeoutSec 改为从配置读取。
constexpr double kOrbitClientExpectedNormalCpu = 0.1;
constexpr double kOrbitClientExpectedNormalMemoryMb = 64.0;
constexpr uint32_t kOrbitClientStartupTimeoutSec = 30;
constexpr uint32_t kOrbitClientHeartbeatTimeoutSec = 15;
}  // namespace

orbit_room_manager::orbit_room_manager() = default;

orbit_room_manager::~orbit_room_manager() = default;

int orbit_room_manager::init() {
  // 后续在此初始化房间状态机 timer 等
  is_inited_ = true;
  return 0;
}

int orbit_room_manager::reload() {
  // 后续 reload 阶段超时配置等
  return 0;
}

int orbit_room_manager::tick() {
  if (!is_inited_) {
    return 0;
  }
  // 驱动房间超时推进；后续清理过期/已退出房间
  int64_t now = util::time::time_utility::get_now();
  rpc::context ctx{rpc::context::create_without_task()};
  for (auto iter = room_index_by_client_id_.begin(); iter != room_index_by_client_id_.end();) {
    if (!iter->second) {
      iter = room_index_by_client_id_.erase(iter);
      continue;
    }
    if (iter->second->get_status() == PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT) {
      // 已退出：步骤 4 先直接移除，后续接入 FINISHING/CANCELING 延迟
      iter = room_index_by_client_id_.erase(iter);
      continue;
    }
    iter->second->tick(ctx, now);
    ++iter;
  }
  return 0;
}

int orbit_room_manager::stop() {
  if (!is_inited_) {
    return 0;
  }
  is_closing_ = true;
  // 后续打印运行中房间
  room_index_by_client_id_.clear();
  is_inited_ = false;
  return 0;
}

int orbit_room_manager::cleanup() {
  room_index_by_client_id_.clear();
  return 0;
}

std::shared_ptr<orbit_room> orbit_room_manager::get_room(const std::string& client_id) noexcept {
  auto iter = room_index_by_client_id_.find(client_id);
  if (iter == room_index_by_client_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

int32_t orbit_room_manager::create_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq& req,
                                        PROJECT_NAMESPACE_ID::SSOrbitCreateRoomRsp& rsp) {
  const std::string& client_id = req.room_key().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit_room_manager create_room failed, client_id is empty");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  if (room_index_by_client_id_.count(client_id) != 0) {
    FWLOGERROR("orbit_room_manager create_room failed, client_id {} already exists", client_id);
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  auto room = std::make_shared<orbit_room>(req.room_key(), req.room_data());
  int32_t ret = room->create(ctx);
  if (ret != 0) {
    FWLOGERROR("orbit_room_manager create_room failed, client_id: {}, ret: {}", client_id, ret);
    rsp.set_result_code(ret);
    return ret;
  }
  room_index_by_client_id_[client_id] = room;

  // Orbit 拉起 Client（client_id 即房间标识）
  auto invoke_result = rpc::async_invoke(
      ctx, "orbit_room_manager.create_room.start_client",
      [room](rpc::context& child_ctx) -> rpc::result_code_type {
        orbit::DAgentClientStartArgs args;
        args.mutable_client_start_args()->mutable_client_id()->set_client_id(room->get_client_id());
        args.mutable_resource()->set_normal_cpu(kOrbitClientExpectedNormalCpu);
        args.mutable_resource()->set_normal_memory_mb(kOrbitClientExpectedNormalMemoryMb);
        args.set_startup_timeout_sec(kOrbitClientStartupTimeoutSec);
        args.set_heartbeat_timeout_sec(kOrbitClientHeartbeatTimeoutSec);

        const std::string& region = room->get_room_data().has_map_data() ? room->get_room_data().map_data().region()
                                                                        : std::string();

        int32_t sub_ret =
            RPC_AWAIT_CODE_RESULT(orbit_server_manager::me()->start_client(child_ctx, region, args, ""));
        if (sub_ret != 0) {
          FWLOGERROR("orbit_room {} start_client failed, ret: {}", room->get_client_id(), sub_ret);
          room->on_client_end(child_ctx);
          RPC_RETURN_CODE(sub_ret);
        }
        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FWLOGERROR("orbit_room {} invoke start_client failed, result: {}", client_id, *invoke_result.get_error());
    room->on_client_end(ctx);
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
  }

  rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  return 0;
}

int32_t orbit_room_manager::join_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomReq& req,
                                      PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomRsp& rsp) {
  // ===== 用户待办（TODO-USER）：join_room 按房间定位 =====
  // 现状：SSOrbitUserJoinRoomReq 未携带 room_key，本实现只能取「首个 CLIENT_LOADED 房间」占位。
  // 需要你（协议所有者）决定：
  //   1) 在 SSOrbitUserJoinRoomReq 中补充 room_key（推荐：DOrbitRoomKey room_key = 1;，字段后移 user_init_datas）；
  //   2) 或明确 gamesvr 侧入房协议改为携带 client_id。
  // 协议确定后告知我，我会把 join_room 改为按 room_key.client_id() 定位房间（与 subscribe/heartbeat 一致）。
  if (room_index_by_client_id_.empty()) {
    FWLOGERROR("orbit_room_manager join_room failed, no room exists");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  // 占位：取第一个房间（CLIENT_LOADED 状态）作为目标
  std::shared_ptr<orbit_room> target_room;
  for (auto& pair : room_index_by_client_id_) {
    if (pair.second && pair.second->get_status() == PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADED) {
      target_room = pair.second;
      break;
    }
  }
  if (!target_room) {
    FWLOGERROR("orbit_room_manager join_room failed, no CLIENT_LOADED room");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  int32_t ret = target_room->join_users(ctx, req.user_init_datas());
  rsp.set_result_code(ret);
  return ret;
}

int32_t orbit_room_manager::subscribe_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitSubscribeRoomReq& req,
                                           PROJECT_NAMESPACE_ID::SSOrbitSubscribeRoomRsp& rsp) {
  if (!req.has_room_key() || req.room_key().client_id().empty()) {
    FWLOGERROR("orbit_room_manager subscribe_room failed, room_key is empty");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  std::shared_ptr<orbit_room> room = get_room(req.room_key().client_id());
  if (!room) {
    FWLOGWARNING("orbit_room_manager subscribe_room, room {} not found", req.room_key().client_id());
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  int32_t ret = room->get_wal_handle()->subscribe(ctx, req.user_key(), req.acknowledge_event_id());
  rsp.set_result_code(ret);
  return ret;
}

int32_t orbit_room_manager::unsubscribe_room(rpc::context& ctx,
                                             const PROJECT_NAMESPACE_ID::SSOrbitUnsubscribeRoomReq& req,
                                             PROJECT_NAMESPACE_ID::SSOrbitUnsubscribeRoomRsp& rsp) {
  if (!req.has_room_key() || req.room_key().client_id().empty()) {
    FWLOGERROR("orbit_room_manager unsubscribe_room failed, room_key is empty");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  std::shared_ptr<orbit_room> room = get_room(req.room_key().client_id());
  if (!room) {
    FWLOGWARNING("orbit_room_manager unsubscribe_room, room {} not found", req.room_key().client_id());
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  int32_t ret = room->get_wal_handle()->unsubscribe(ctx, req.user_key());
  rsp.set_result_code(ret);
  return ret;
}

int32_t orbit_room_manager::heartbeat(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitHeartbeatReq& req,
                                      PROJECT_NAMESPACE_ID::SSOrbitHeartbeatRsp& rsp) {
  if (!req.has_room_key() || req.room_key().client_id().empty()) {
    FWLOGERROR("orbit_room_manager heartbeat failed, room_key is empty");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  std::shared_ptr<orbit_room> room = get_room(req.room_key().client_id());
  if (!room) {
    FWLOGWARNING("orbit_room_manager heartbeat, room {} not found", req.room_key().client_id());
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  int32_t ret = room->get_wal_handle()->update_acknowledge(ctx, req.user_key(), req.acknowledge_event_id());
  rsp.set_result_code(ret);
  return ret;
}

int32_t orbit_room_manager::get_player_info(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitGetPlayerInfoReq& req,
                                            PROJECT_NAMESPACE_ID::SSOrbitGetPlayerInfoRsp& rsp) {
  if (!req.has_room_key() || req.room_key().client_id().empty()) {
    FWLOGERROR("orbit_room_manager get_player_info failed, room_key is empty");
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  std::shared_ptr<orbit_room> room = get_room(req.room_key().client_id());
  if (!room) {
    FWLOGWARNING("orbit_room_manager get_player_info, room {} not found", req.room_key().client_id());
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  const PROJECT_NAMESPACE_ID::DOrbitUserInitData* user_data = room->get_user_init_data(req.user_key());
  if (nullptr == user_data) {
    FWLOGWARNING("orbit_room_manager get_player_info, user {},{} not in room {}", req.user_key().user_id(),
                 req.user_key().zone_id(), req.room_key().client_id());
    rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  rsp.mutable_room_key()->CopyFrom(req.room_key());
  *rsp.add_user_init_datas() = *user_data;
  rsp.set_result_code(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  return 0;
}

void orbit_room_manager::on_client_start(const std::string& client_id, const std::string& client_addr,
                                         const std::string& payload) {
  FWLOGINFO("orbit_room_manager on_client_start, client_id: {}, addr: {}, payload size: {}", client_id, client_addr,
            payload.size());
  auto room = get_room(client_id);
  if (!room) {
    FWLOGWARNING("orbit_room_manager on_client_start, room {} not found", client_id);
    return;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  room->on_client_start(ctx, client_addr);
}

void orbit_room_manager::on_client_end(const std::string& client_id, const std::string& payload) {
  FWLOGINFO("orbit_room_manager on_client_end, client_id: {}, payload size: {}", client_id, payload.size());
  auto room = get_room(client_id);
  if (!room) {
    FWLOGWARNING("orbit_room_manager on_client_end, room {} not found", client_id);
    return;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  room->on_client_end(ctx);
}

void orbit_room_manager::on_user_finish(
    const std::string& client_id,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results) {
  FWLOGINFO("orbit_room_manager on_user_finish, client_id: {}, result size: {}", client_id, results.size());
  auto room = get_room(client_id);
  if (!room) {
    FWLOGWARNING("orbit_room_manager on_user_finish, room {} not found", client_id);
    return;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  room->on_user_finish(ctx, results);
}
