// Copyright 2026 atframework

#pragma once

#include <rpc/rpc_common_types.h>
#include <design_pattern/noncopyable.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.match.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <cstdint>
#include <string>

namespace PROJECT_NAMESPACE_ID {
class SSMatchingEventSync;
}

namespace rpc {
class context;
}

class user;

// 玩家级匹配流程入口。它保存本玩家的匹配快照和 WAL 游标，并负责 CS/SS 协议之间的转换。
class user_matching_manager : public atfw::util::design_pattern::noncopyable {
 public:
  explicit user_matching_manager(user& owner);
  ~user_matching_manager();

  // 玩家数据生命周期钩子。匹配视图随 table_user 持久化，以便重登录后恢复订阅。
  void create_init(rpc::context& ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type login_init(rpc::context& ctx);
  void init_from_table_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_user& user_table);
  int dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_user& user_table) const;
  bool is_dirty() const;
  void clear_dirty();

  // CS 匹配操作。操作者身份和 lobbysvr 订阅路由只由服务端填写。
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type start_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingStartReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingStartRsp& response);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type check_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingCheckReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingCheckRsp& response);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type cancel_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingCancelReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingCancelRsp& response);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type confirm_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingConfirmReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp& response);

  // 合并 matchsvr 推送，并将原始日志或全量快照主动转发给在线客户端。
  void acknowledge_matching_sync(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync);

  const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& get_snapshot() const;
  int64_t get_last_event_id() const;

 private:
  // 将同步 RPC 回包写入本地视图，但不把“已应用”误当作“客户端已确认”。
  void update_snapshot(const PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& snapshot);
  // 将一条严格递增的 WAL 日志合并到本地快照。
  void apply_event(const PROJECT_NAMESPACE_ID::DMatchingEventLog& event_log);

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t fill_matching_scope(const PROJECT_NAMESPACE_ID::DLevelSelect& level_select,
                                                           const std::string& battle_version,
                                                           PROJECT_NAMESPACE_ID::DMatchingScope& output) const;
  // 组队未接入前，只组装包含当前登录玩家的单人 unit。
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type fill_matching_unit(
      rpc::context& ctx, PROJECT_NAMESPACE_ID::DMatchingUnit& output) const;

  // 构造只能代表当前登录玩家的内部操作者身份。``
  void fill_operator_user(PROJECT_NAMESPACE_ID::DUserIDKey& output) const;
  // 从服务端持久化快照中查找当前玩家所在 unit，绝不信任客户端提交的 unit_id。
  uint64_t get_current_unit_id() const;
  // 当前 lobbysvr 已知的客户端确认游标，用于断点重放。
  int64_t get_acknowledge_event_id() const;
  // 发送客户端匹配日志流；离线时只保留持久化状态。
  void send_log_sync(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync, bool is_switch);

 private:
  user* owner_;
  PROJECT_NAMESPACE_ID::DUserMatchingData data_;
  std::string pending_switch_matching_id_;
  bool dirty_;
};
