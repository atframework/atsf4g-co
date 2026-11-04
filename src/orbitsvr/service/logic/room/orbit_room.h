// Copyright 2026 atframework
// Created by atsf4g-co orbit_room module migration

#pragma once

#include <config/extern_service_types.h>
#include <dispatcher/task_type_traits.h>
#include <memory/rc_ptr.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>
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

#include <data/user_key_hash_helper.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpc {
class context;
}  // namespace rpc

struct orbit_room_user_data {
  PROJECT_NAMESPACE_ID::DOrbitUserInitData init_data_;
  PROJECT_NAMESPACE_ID::DOrbitUserInitResult init_result_;
  PROJECT_NAMESPACE_ID::DOrbitUserFinishResultFull finish_result_;
  PROJECT_NAMESPACE_ID::DOrbitUserFinishResultCommon finish_result_common_;
  PROJECT_NAMESPACE_ID::DUserIDKey user_key_;
  bool init_ = false;
  int32_t init_retry_count_ = 0;
  bool finish_ = false;
  bool settlement_finish_ = false;
  int64_t finish_timepoint_ = 0;
  int32_t settlement_retry_count_ = 0;
  task_type_trait::task_type settlement_task_;
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
  rpc::result_code_type start_client(rpc::context& ctx, int32_t client_template_id);
  int32_t on_client_start(rpc::context& ctx, const std::string& client_addr);

  int32_t init_user(const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserInitData>& user_list,
                    bool is_last_one);

  int32_t on_user_finish(
      rpc::context& ctx,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResultFull>& results);
  int32_t on_room_end_join(rpc::context& ctx);
  int32_t on_client_end(rpc::context& ctx, atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code);
  rpc::result_code_type on_remote_start_client(rpc::context& ctx,
                                               const PROJECT_NAMESPACE_ID::DOrbitRemoteStartArg& arg);

  bool ready_to_destroy() const;
  void on_destroy();

 private:
  int32_t add_event_log(rpc::context& ctx, PROJECT_NAMESPACE_ID::DOrbitRoomEventLog&& event_log);
  int32_t set_status(PROJECT_NAMESPACE_ID::EnOrbitRoomStatus v);
  int32_t init_user_to_client(rpc::context& ctx);
  // 房间心跳：第一个玩家加入后每分钟一次，直到房间结束
  void tick_room_heartbeat(rpc::context& ctx, int64_t now);

  int32_t room_finish(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason);
  void async_user_settlement(rpc::context& ctx, orbit_room_user_data_ptr_t user_ptr);
  void send_room_end_join_to_match(rpc::context& ctx);
  void send_room_user_quit_to_match(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitUserKey& user_key);
  rpc::result_code_type user_settlement(rpc::context& ctx, orbit_room_user_data_ptr_t user_ptr);

  uint64_t match_server_id_ = 0;
  PROJECT_NAMESPACE_ID::DOrbitRoomKey room_key_;
  PROJECT_NAMESPACE_ID::DOrbitRoomInitData room_data_;
  PROJECT_NAMESPACE_ID::EnOrbitRoomStatus room_status_ = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_INVALID;
  int64_t create_timepoint_ = 0;
  std::string client_address_;
  int64_t expired_timepoint_ = 0;
  int64_t loading_timeout_ = 0;
  int64_t join_end_timepoint_ = 0;    // TODO(yousongyang) 删除
  uint32_t end_join_sec_config_ = 0;  // TODO(yousongyang) 删除

  PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason_ = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_FINISH;
  bool client_end_ = false;
  bool init_user_finish_ = false;
  bool init_finish_notified_ = false;  // 是否已补发过仅置位 init_finish 的 user_init
  bool need_retry_settlement_ = false;
  bool end_join_ = false;  // DS退出或者DS通知EndJoin
  bool end_join_send_ = false;
  size_t init_to_client_finish_count_ = 0;
  bool has_user_joined_ = false;               // 是否已有玩家注册成功，房间心跳从此时开始
  int64_t next_room_heartbeat_timepoint_ = 0;  // 下一次房间心跳的时间点
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, orbit_room_user_data_ptr_t, user_key_hash_t, user_key_equal_t>
      user_data_index_;
  task_type_trait::task_type init_to_client_task_;
  std::string subscriber_key_;
  rpc::dtmq::client_subscriber::ptr_t subscriber_;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> finish_user_list_;
  atfw::dtmq::DChannelIdKey channel_key_;
};
