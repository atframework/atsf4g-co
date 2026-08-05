// Copyright 2026 atframework
// Created by atsf4g-co battle module migration

#pragma once

#include <design_pattern/singleton.h>

#include <rpc/rpc_common_types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <config/extern_service_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.orbit.pb.h>
#include <protocol/pbdesc/orbit_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "logic/room/orbit_room.h"

namespace rpc {
class context;
}  // namespace rpc

// 战斗房间管理器（裁剪版，内嵌于 orbitsvr）
// 职责：房间生命周期、精简阶段状态机
//   EnOrbitRoomStatus: CLIENT_LOADING -> CLIENT_LOADED -> USER_INITING -> USER_RUNNING -> USER_FINISH -> EXIT
// 步骤 4：create_room（matchsvr）建房并拉起 Client、on_client_start（Client 就绪）、
//   join_room（gamesvr）用户初始化、on_client_end（退出处理）。
class orbit_room_manager : public util::design_pattern::singleton<orbit_room_manager> {
 public:
  orbit_room_manager();
  ~orbit_room_manager();

  int init();
  int reload();
  int tick();
  int stop();
  int cleanup();

  inline int32_t get_room_size() const noexcept { return static_cast<int32_t>(room_index_by_client_id_.size()); }

  // ---- SS 入站（由 task_action_* 调用） ----
  // 建房（matchsvr 调用）：创建 orbit_room + Orbit 拉起 Client
  int32_t create_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq& req,
                      PROJECT_NAMESPACE_ID::SSOrbitCreateRoomRsp& rsp);
  // 用户入房（gamesvr 调用）：收集用户 -> user_init
  int32_t join_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomReq& req,
                    PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomRsp& rsp);
  // 订阅 / 反订阅房间（gamesvr 调用）：对接 WAL handle subscribe / unsubscribe
  int32_t subscribe_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitSubscribeRoomReq& req,
                         PROJECT_NAMESPACE_ID::SSOrbitSubscribeRoomRsp& rsp);
  int32_t unsubscribe_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitUnsubscribeRoomReq& req,
                           PROJECT_NAMESPACE_ID::SSOrbitUnsubscribeRoomRsp& rsp);
  // 心跳对账（gamesvr 调用）：推进 WAL acknowledge_event_id
  int32_t heartbeat(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitHeartbeatReq& req,
                    PROJECT_NAMESPACE_ID::SSOrbitHeartbeatRsp& rsp);
  // 拉取玩家初始化数据（gamesvr 调用）
  int32_t get_player_info(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitGetPlayerInfoReq& req,
                          PROJECT_NAMESPACE_ID::SSOrbitGetPlayerInfoRsp& rsp);

  // ---- Orbit Client 回调入口（由 orbitsvr main 的 orbit_server_manager 回调转发） ----
  // Client 进程启动成功：client_id 即 DOrbitRoomKey.client_id（房间标识）
  void on_client_start(const std::string& client_id, const std::string& client_addr, const std::string& payload);
  // Client 进程退出：client_id 即房间标识
  void on_client_end(const std::string& client_id, const std::string& payload);

  // Client 上报对局结束（OrbitServerRpcService.user_finish 入站转发，步骤 5 实现）
  void on_user_finish(
      const std::string& client_id,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results);

  // 按 client_id 取房间
  std::shared_ptr<orbit_room> get_room(const std::string& client_id) noexcept;

 private:
  bool is_inited_ = false;
  bool is_closing_ = false;

  // 房间索引：client_id -> orbit_room（房间数据全部内存持有，需求 #9）
  std::map<std::string, std::shared_ptr<orbit_room>> room_index_by_client_id_;
};
