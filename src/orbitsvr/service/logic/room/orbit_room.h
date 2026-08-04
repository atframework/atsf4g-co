// Copyright 2026 atframework
// Created by atsf4g-co battle module migration

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <config/extern_service_types.h>
#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.orbit.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <data/player_key_hash_helper.h>

namespace rpc {
class context;
}  // namespace rpc

class orbit_room_wal_handle;

// 战斗房间（裁剪版，内嵌于 orbitsvr）
// 状态机（EnOrbitRoomStatus）：
//   CLIENT_LOADING -> CLIENT_LOADED -> USER_INITING -> USER_RUNNING -> USER_FINISH -> EXIT
// 步骤 4 职责：建房拉起 Client（CLIENT_LOADING）、Client 就绪（CLIENT_LOADED）、
//   用户初始化（USER_INITING -> user_init -> USER_RUNNING）、退出处理（EXIT）。
class orbit_room : public std::enable_shared_from_this<orbit_room> {
 public:
  orbit_room(const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
             const PROJECT_NAMESPACE_ID::DOrbitRoomInitData& room_data);
  ~orbit_room();

  const std::string& get_client_id() const noexcept;
  const PROJECT_NAMESPACE_ID::DOrbitRoomKey& get_room_key() const noexcept;
  const PROJECT_NAMESPACE_ID::DOrbitRoomInitData& get_room_data() const noexcept;
  PROJECT_NAMESPACE_ID::EnOrbitRoomStatus get_status() const noexcept;

  // 建房：进入 CLIENT_LOADING，写 start_loading 事件
  int32_t create(rpc::context& ctx);
  // Client 启动成功：进入 CLIENT_LOADED，写 finish_loading 事件
  int32_t on_client_start(rpc::context& ctx, const std::string& client_addr);
  // 用户入房：收集用户 -> USER_INITING -> 调 Client user_init -> USER_RUNNING
  int32_t join_users(rpc::context& ctx,
                     const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserInitData>& users);
  // Client 上报对局结束（步骤 5）：逐个用户写 user_finish 事件 -> USER_FINISH，
  // 每个玩家组装 DOrbitUserFinishAsyncData -> rpc::async_jobs::add_jobs(orbit_finish)，最后 EXIT
  int32_t on_user_finish(
      rpc::context& ctx,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results);
  // Client 退出：进入 EXIT，写 client_exit 事件
  int32_t on_client_end(rpc::context& ctx,
                        PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason =
                            PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_UNKNOWN);

  // 生成房间快照
  void dump(PROJECT_NAMESPACE_ID::DOrbitRoomSnapshotData& out) const;
  // 超时推进（tick 由 orbit_room_manager 驱动）
  int32_t tick(rpc::context& ctx, int64_t now);

  const std::shared_ptr<orbit_room_wal_handle>& get_wal_handle() const noexcept { return wal_handle_; }

 private:
  int32_t set_status(PROJECT_NAMESPACE_ID::EnOrbitRoomStatus v);

  PROJECT_NAMESPACE_ID::DOrbitRoomKey room_key_;
  PROJECT_NAMESPACE_ID::DOrbitRoomInitData room_data_;
  PROJECT_NAMESPACE_ID::EnOrbitRoomStatus status_ = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_INVALID;
  int64_t create_timepoint_ = 0;
  int64_t status_end_timepoint_ = 0;
  std::string client_address_;

  // 用户：user_key -> 初始 init 数据
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, PROJECT_NAMESPACE_ID::DOrbitUserInitData, player_key_hash_t,
                     player_key_equal_t>
      user_index_;
  // 用户初始化结果：user_key -> token
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, PROJECT_NAMESPACE_ID::DOrbitUserInitResult, player_key_hash_t,
                     player_key_equal_t>
      user_init_result_index_;
  // 对局结束上报的玩家完成结果（步骤 5，供快照/结算上下文使用）
  std::vector<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult> user_finish_results_;

  std::shared_ptr<orbit_room_wal_handle> wal_handle_;
};
