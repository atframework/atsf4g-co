// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>

#include <string>
#include <unordered_map>

namespace rpc {
class context;
}

PROJECT_NAMESPACE_BEGIN

class DOrbitRoomKey;
class DOrbitRoomSnapshotData;
class table_user;

PROJECT_NAMESPACE_END

class user;

class user_orbit_manager {
 public:
  explicit user_orbit_manager(user& owner);
  void refresh_feature_limit_second(rpc::context& ctx);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type login_init(rpc::context&);

  user& get_owner() { return *owner_; }
  const user& get_owner() const { return *owner_; }

  void fetch_user_data(PROJECT_NAMESPACE_ID::DOrbitRoomUserData& user_data) const;
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);
  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user_table) const;

  // 开始匹配前需要确认是否还存在房间
  bool is_orbit_room_exist() const;
  // Orbitsvr已经创建并塞入了User 开始真正进入Orbitsvr
  rpc::result_code_type join_orbit_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                                        int64_t expired_timepoint);
  // 收到结算消息
  void receive_orbit_settlement(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitUserFinishAsyncData& finish_data);
  // 组装历史数据
  void load_orbit_room_snapshot(rpc::context& ctx, rpc::dtmq::client_subscriber::ptr_t subscriber);
  // 收到事件消息
  void on_receive_event(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                        const ::atfw::dtmq::DChannelMessage& data);

 private:
  void clear_orbit_room_data();
  void on_receive_event(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog& event_log);
  void mark_dirty();
  int32_t create_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key, int64_t expired_timepoint);
  user* ATFW_UTIL_MACRO_NONNULL owner_;

  struct orbit_room_data {
    bool is_joined_ = false;
    PROJECT_NAMESPACE_ID::EnOrbitRoomStatus room_status_;

    PROJECT_NAMESPACE_ID::DOrbitRoomInit init_data_;          // room_init
    PROJECT_NAMESPACE_ID::DOrbitRoomExitInfo exit_info_;      // client_exit
    PROJECT_NAMESPACE_ID::DOrbitRoomReady ready_data_;        // DOrbitRoomReady
    PROJECT_NAMESPACE_ID::DOrbitUserInitResult init_result_;  // user_init_success
  };
  using orbit_room_data_ptr = atfw::util::memory::strong_rc_ptr<orbit_room_data>;
  std::string subscriber_key_;
  bool dirty_ = false;

  PROJECT_NAMESPACE_ID::DOrbitRoomKey room_key_;
  int64_t orbit_room_expired_timepoint_ = 0;
  rpc::dtmq::client_subscriber::ptr_t subscriber_;
  orbit_room_data_ptr room_data_;
};
