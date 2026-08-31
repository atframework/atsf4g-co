// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>
#include <nostd/nullability.h>
#include <rpc/rpc_common_types.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.match.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <cli/cmd_option_list.h>
#include <data/user_type_define.h>

#include <cstdint>
#include <string>
#include <vector>

namespace PROJECT_NAMESPACE_ID {
class SSMatchingEventSync;
}

namespace rpc {
class context;
}

class user;

// 玩家级匹配流程入口。它保存本玩家的内部匹配视图和 WAL 游标，并负责生成不含房间信息的客户端视图。
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
  // 玩家持有有效内部 matching_id 且房间处于搜索、确认或创建战斗阶段。
  bool is_in_matching() const;

  // 是否在流程中
  bool is_in_orbit_or_matching() const;

  // CS 匹配操作。操作者身份和 lobbysvr 订阅路由只由服务端填写。
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type start_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingStartReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingStartRsp& response);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type check_matching(rpc::context& ctx,
                                                                    PROJECT_NAMESPACE_ID::SCMatchingCheckRsp& response);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type cancel_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingCancelReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingCancelRsp& response);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type confirm_matching(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::CSMatchingConfirmReq& request,
      PROJECT_NAMESPACE_ID::SCMatchingConfirmRsp& response);

  // 合并 matchsvr 推送，并将权威客户端视图主动转发给在线客户端。
  void acknowledge_matching_sync(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSMatchingEventSync& sync);

  const PROJECT_NAMESPACE_ID::DMatchingPlayerView& get_view() const;
  PROJECT_NAMESPACE_ID::DMatchingClientView get_client_view() const;
  int64_t get_last_event_id() const;

 private:
  // 合并 Matchsvr 玩家视图；同一房间内拒绝早于本地 WAL 游标的旧视图。
  void update_view(const PROJECT_NAMESPACE_ID::DMatchingPlayerView& view);
  void clear_matching_state();
  void dump_dirty_data(PROJECT_NAMESPACE_ID::DMatchingClientViewDirtyChg& output) const;
  void dump_client_view(PROJECT_NAMESPACE_ID::DMatchingClientView& output) const;

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t fill_matching_scope(const PROJECT_NAMESPACE_ID::DLevelSelect& level_select,
                                                           const std::string& battle_version,
                                                           PROJECT_NAMESPACE_ID::DMatchingScope& output,
                                                           std::vector<int32_t>& acceptable_level_ids) const;
  // 组队未接入前，只组装包含当前登录玩家的单人 unit。
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type fill_matching_unit(
      rpc::context& ctx, PROJECT_NAMESPACE_ID::DMatchingUnit& output) const;

  // 构造只能代表当前登录玩家的内部操作者身份。``
  void fill_operator_user(PROJECT_NAMESPACE_ID::DUserIDKey& output) const;
  // 从服务端持久化视图中查找当前玩家所在 unit。
  uint64_t get_current_unit_id() const;
  // 当前 lobbysvr 已应用的 matchsvr WAL 游标，用于断点重放。
  int64_t get_acknowledge_event_id() const;

  void on_client_view_changed(rpc::context& ctx);

 public:
  static void on_gm_cmd_start_matching(std::shared_ptr<rpc::context> ctx, user_ptr_t user_inst,
                                       std::shared_ptr<PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> rsp,
                                       ::util::cli::cmd_option_list& params);

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  PROJECT_NAMESPACE_ID::DUserMatchingData data_;
  std::string pending_switch_matching_id_;
  bool dirty_;
};
