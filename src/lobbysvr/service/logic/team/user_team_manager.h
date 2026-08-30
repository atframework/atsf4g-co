// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

#include <memory/rc_ptr.h>

#include <nostd/function_ref.h>
#include <nostd/nullability.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

#include <rpc/team/team_key_hash_helper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.shared.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstddef>
#include <list>
#include <unordered_map>

#include "logic/team/user_team.h"

class user;

PROJECT_NAMESPACE_BEGIN
class table_user;
PROJECT_NAMESPACE_END

class user_team_manager {
 public:
  using team_join_request_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamJoinRequest>;
  using team_invitation_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamInvitation>;

  struct team_group {
    user_team::ptr_t current;

    // 同组队伍只能存在一个
    std::unordered_map<atfw::team::DTeamKey, user_team::ptr_t, rpc::team::team_api::team_key_hash_t,
                       rpc::team::team_api::team_key_equal_t>
        pending_to_exit;
  };

 public:
  explicit user_team_manager(user& owner);
  ~user_team_manager();

  // 创建默认角色数据
  void create_init(rpc::context& ctx);

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t login_init(rpc::context&);

  void refresh_feature_limit_second(rpc::context&);
  void refresh_feature_limit_minute(rpc::context&);

  // 从table数据初始化
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);

  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& table) const;

  bool is_dirty() const;

  void clear_dirty();

  inline user& get_owner() { return *owner_; }
  inline const user& get_owner() const { return *owner_; }

  void foreach_running_team(
      atfw::util::nostd::function_ref<void(uint32_t, const atfw::util::nostd::nonnull<user_team::ptr_t>&)>) const;

  team_join_request_ptr_t get_pending_join_request(const atfw::team::DTeamKey& team_key) const noexcept;

  team_invitation_ptr_t get_pending_invitation(const atfw::team::DTeamKey& team_key) const noexcept;
  // 个人频道已处理事件水位(随 table 落地, 用于去重迟到事件)
  inline int64_t get_processed_private_chat_channel_sequence() const noexcept {
    return processed_private_chat_channel_sequence_;
  }

  // 同意收到的邀请(协程内调用): 打包 SSTeamRoomApproveInvitationReq 并转发到 teamsvr-room(按队伍一致性哈希路由)，
  // 成员数据(客户端版本/路由)由被邀请人在同意时上报，业务结果经 client_result 透传
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type approve_invitation(rpc::context& ctx,
                                                                        const team_invitation_ptr_t& invitation);

  // 拒绝收到的邀请(协程内调用): 打包 SSTeamRoomRejectInvitationReq 并转发到 teamsvr-room(按队伍一致性哈希路由)，
  // 业务结果经 client_result 透传
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_invitation(rpc::context& ctx,
                                                                       const team_invitation_ptr_t& invitation);

  // 发起加入请求(协程内调用): 打包 SSTeamRoomAddJoinRequestReq 并转发到 teamsvr-room(按队伍一致性哈希路由)，
  // 申请人的版本/路由/私有频道由本人上报，业务结果经 client_result 透传
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_join_request(rpc::context& ctx,
                                                                       const atfw::team::DTeamKey& team_key,
                                                                       atfw::team::EnTeamSourceType team_source_type,
                                                                       const ::google::protobuf::Any& team_source_data);

  // 创建队伍(协程内调用): 打包 SSTeamRoomCreateReq 并转发到 teamsvr-room(team_id 由服务端分配)，
  // 创建者作为队长(OWNER)入队，初始 shared_team_data/shared_member_data 随请求上报；
  // 成功后直接按响应在本地注册队伍(create 不会回发 joined_team 通知)并输出 team_key，业务结果经 client_result 透传
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type create_team(rpc::context& ctx,
                                                                 PROJECT_NAMESPACE_ID::EnTeamType type,
                                                                 atfw::team::DTeamKey& output_team_key);

  // 发起邀请(协程内调用): 打包 SSTeamRoomAddInvitationReq 并转发到 teamsvr-room(按队伍一致性哈希路由)，
  // 被邀请人的私有通知频道由其 user_key 派生，业务结果经 client_result 透传
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_invitation(rpc::context& ctx,
                                                                     const atfw::team::DTeamKey& team_key,
                                                                     const PROJECT_NAMESPACE_ID::DUserIDKey& invitee,
                                                                     atfw::team::EnTeamSourceType team_source_type,
                                                                     const ::google::protobuf::Any& team_source_data);

  user_team::ptr_t get_team_by_team_key(const atfw::team::DTeamKey& team_key) const noexcept;

  user_team::ptr_t get_team_by_team_type(PROJECT_NAMESPACE_ID::EnTeamType type) const noexcept;

  void remove_team(rpc::context& ctx, const atfw::team::DTeamKey& team_key, atfw::team::EnTeamExitReason exit_reason);

  void pack_team_shared_data(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType type,
                             ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output);

  void pack_team_member_shared_data(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType type,
                                    ::google::protobuf::RepeatedPtrField<::atfw::team::DTeamAnyDataWithKey>& output);

 private:
  void set_processed_private_chat_channel_sequence(int64_t sequence);

  void add_team(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnTeamType team_type,
                const atfw::team::DTeamMemberJoinData& join_data);
  void remove_team(rpc::context& ctx, const atfw::team::DTeamKey& team_key, bool send_exit,
                   atfw::team::EnTeamExitReason exit_reason);

  size_t cleanup_expired_join_request(rpc::context& ctx);
  bool add_pending_join_request(rpc::context& ctx, const team_join_request_ptr_t& join_request);
  bool remove_pending_join_request(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

  size_t cleanup_expired_invitation(rpc::context& ctx);
  bool add_pending_invitation(rpc::context& ctx, const team_invitation_ptr_t& invitation);
  bool remove_pending_invitation(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;

  bool is_dirty_;

  int64_t processed_private_chat_channel_sequence_;

  friend class user_team_manager_utility;
  // user_team 在频道销毁(WAL kDestroy)等已知 room 已不存在的路径上需要跳过退出上报的重载
  friend class user_team;

  // 可以同时在多个队伍（分组）中，但同组队伍只能存在一个
  std::unordered_map<uint32_t, team_group> team_group_;
  std::unordered_map<atfw::team::DTeamKey, user_team::ptr_t, rpc::team::team_api::team_key_hash_t,
                     rpc::team::team_api::team_key_equal_t>
      team_index_;

  std::list<team_join_request_ptr_t> pending_join_request_by_expired_time_;
  std::unordered_map<atfw::team::DTeamKey, std::list<team_join_request_ptr_t>::iterator,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      pending_join_request_by_team_id_;

  std::list<team_invitation_ptr_t> pending_invitation_by_expired_time_;
  std::unordered_map<atfw::team::DTeamKey, std::list<team_invitation_ptr_t>::iterator,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      pending_invitation_by_team_id_;
};
