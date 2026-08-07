// Copyright 2026 atframework
// Created by atsf4g-co orbit_room module migration

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <config/extern_service_types.h>
#include <dispatcher/task_type_traits.h>
#include <memory/rc_ptr.h>
#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.dtmq.common.pb.h>
#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/com.struct.orbit.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <data/player_key_hash_helper.h>

namespace rpc {
class context;
}  // namespace rpc

enum EnOrbitRoomUserStatus {
  EN_ORBIT_ROOM_USER_STATUS_INVALID = 0,
  EN_ORBIT_ROOM_USER_STATUS_INIT_FROM_MATCH = 1,  // 注册玩家ID到服务
  EN_ORBIT_ROOM_USER_STATUS_INIT_FROM_LOBBY = 2,  // 注册玩家数据到服务
  EN_ORBIT_ROOM_USER_STATUS_INIT_TO_CLIENT = 3,   // 注册玩家数据到客户端
  EN_ORBIT_ROOM_USER_STATUS_FINISH_CLIENT = 4,    // 客户端完成
};

struct orbit_room_user_data {
  EnOrbitRoomUserStatus user_status_ = EN_ORBIT_ROOM_USER_STATUS_INVALID;
  PROJECT_NAMESPACE_ID::DOrbitUserInitData init_data_;
  PROJECT_NAMESPACE_ID::DOrbitUserInitResult init_result_;
  PROJECT_NAMESPACE_ID::DOrbitUserFinishResult finish_result_;
  PROJECT_NAMESPACE_ID::DUserIDKey user_key_;
  bool init_ = false;
  bool finish_ = false;
  bool settlement_finish_ = false;
  int64_t finish_timepoint_ = 0;
  int32_t settlement_retry_count_ = 0;
  task_type_trait::task_type settlement_task;
};

using orbit_room_user_data_ptr_t = atfw::util::memory::strong_rc_ptr<orbit_room_user_data>;

class orbit_room : public atfw::util::memory::enable_shared_rc_from_this<orbit_room> {
 public:
  orbit_room(const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
             const PROJECT_NAMESPACE_ID::DOrbitRoomInitData& room_data);

  void tick();

  const std::string& get_client_id() const noexcept;
  const std::string& get_region() const;

  int32_t create(rpc::context& ctx, uint64_t match_server_id);
  int32_t init_user(const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserKey>& user_keys,
                    bool is_last_one);
  rpc::result_code_type start_client(rpc::context& ctx, const orbit::DAgentClientStartArgs& args);
  int32_t on_client_start(rpc::context& ctx, const std::string& client_addr);
  rpc::result_code_type join_users(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitUserInitData& user_init_data);

  int32_t on_user_finish(
      rpc::context& ctx,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results);
  int32_t on_client_end(rpc::context& ctx, orbit::EnClientExitReason exit_reason, int32_t exit_code);

  bool ready_to_destroy() const;
  void on_destroy();

 private:
  int32_t add_event_log(rpc::context& ctx, PROJECT_NAMESPACE_ID::DOrbitRoomEventLog&& event_log);
  int32_t set_status(PROJECT_NAMESPACE_ID::EnOrbitRoomStatus v);
  void dump(PROJECT_NAMESPACE_ID::DOrbitRoomSnapshotData& out) const;

  int32_t room_finish(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason);
  void async_user_settlement(rpc::context& ctx, orbit_room_user_data_ptr_t user_ptr);
  rpc::result_code_type user_settlement(rpc::context& ctx, orbit_room_user_data_ptr_t user_ptr);

  uint64_t match_server_id_ = 0;
  PROJECT_NAMESPACE_ID::DOrbitRoomKey room_key_;
  PROJECT_NAMESPACE_ID::DOrbitRoomInitData room_data_;
  PROJECT_NAMESPACE_ID::EnOrbitRoomStatus room_status_ = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_INVALID;
  int64_t create_timepoint_ = 0;
  int64_t status_end_timepoint_ = 0;
  std::string client_address_;
  int64_t expired_timepoint_ = 0;

  PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason_ = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_FINISH;
  bool client_end_ = false;
  bool init_user_finish_ = false;
  bool need_retry_settlement_ = false;
  int32_t join_user_finish_count_ = 0;
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, orbit_room_user_data_ptr_t, player_key_hash_t,
                     player_key_equal_t>
      user_data_index_;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> finish_user_list_;
  atfw::dtmq::DChannelIdKey channel_key_;
};
