// Copyright 2026 atframework
//
// teamsvr-room 基础设施、创建与路由用例(TEAM_ROOM_TEST_PLAN.md §4.1):
//   INF-01~04: fixture 启动/就绪快照/manager 清理/typed action 驱动
//   CRT-01~04: 创建队伍/UUID 路径/非法参数与重复创建/响应丢失恢复
//   ROU-01~02: 本地路由/远端转发

#include "teamsvr_room_test_common.h"  // NOLINT: build/include_subdir

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframework/testing/ss_action.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <logic/action/task_action_create.h>
#include <logic/action/task_action_heartbeat.h>

#include <rpc/team/team_common_api.h>
#include <rpc/team/teamroomservice.atfw.gen.h>

#include <string>

namespace {
using teamsvr_room_test::fake_team_room_channel;
using teamsvr_room_test::kDtmqProxyNodeId;
using teamsvr_room_test::kLocalRoomNodeId;
using teamsvr_room_test::kTeamRoomChannelType;
using teamsvr_room_test::kTestZoneId;
using teamsvr_room_test::make_personal_channel;
using teamsvr_room_test::make_team_key;
using teamsvr_room_test::make_user_key;
using teamsvr_room_test::next_test_team_id;
using teamsvr_room_test::room_test_env;

// 权限失败统一门禁: 预期错误码 + team 房间频道与个人频道零写入
void expect_zero_write(fake_team_room_channel& fake, size_t send_before, size_t update_before, size_t reset_before,
                       size_t destroy_before, const room_test_env& env, size_t personal_before) {
  CASE_EXPECT_EQ(send_before, fake.send_message_calls());
  CASE_EXPECT_EQ(update_before, fake.update_calls());
  CASE_EXPECT_EQ(reset_before, fake.reset_lock_calls());
  CASE_EXPECT_EQ(destroy_before, fake.destroy_calls());
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());
}
}  // namespace

// ============ INF-01: fixture 启动，type 11 配置生效，订阅心跳后 ready，with_private_data ============
CASE_TEST(teamsvr_room_infrastructure, subscribe_ready_with_private_data) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 订阅者就绪(心跳期间 mock 推送了就绪快照)
  CASE_EXPECT_TRUE(room->is_subscriber_ready());
  auto& fake = env.channel(team_id);
  CASE_EXPECT_TRUE(fake.is_created());
  CASE_EXPECT_TRUE(fake.snapshot_pushed());

  // 建立房间后 manager 持有一个 room；房间未创建队伍且锁为空时不是主控
  CASE_EXPECT_EQ(1u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_FALSE(room->is_lock_holder());

  // 用例结束清理 manager 后无残留
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ INF-02: ready 快照的锁/custom/private data 和增量日志能送达 room 回调 ============
CASE_TEST(teamsvr_room_infrastructure, ready_snapshot_delivery) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();

  // 预置频道状态: custom/private data + 一个他人持有的锁
  auto& fake = env.channel(team_id);
  fake.ensure_created();

  atfw::team::DTeamStorage storage;
  protobuf_copy_message(*storage.mutable_team_key(), make_team_key(team_id));
  auto* member = storage.add_member();
  protobuf_copy_message(*member->mutable_user_key(), make_user_key(1, 1001));
  member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  auto now_tp = protobuf_from_system_clock(atfw::util::time::time_utility::now());
  *member->mutable_joined_timepoint() = now_tp;
  *member->mutable_last_heartbeat_timepoint() = now_tp;
  fake.set_custom_data(storage);

  atfw::team::DTeamRoomPrivateData private_data;
  private_data.set_team_created(true);
  fake.set_private_data(private_data);

  fake.mutable_lock().set_lock_holder("another-node");
  *fake.mutable_lock().mutable_timeout() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{3600});

  auto room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 快照恢复后: 成员已恢复，且本节点因他人持锁而非主控
  CASE_EXPECT_TRUE(room->find_member(make_user_key(1, 1001), false) != nullptr);
  CASE_EXPECT_FALSE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ INF-03/BND-03: 完整 DTeamKey 复用、跨区隔离、非法 key 拒绝与清理 ============
CASE_TEST(teamsvr_room_infrastructure, mutable_room_reuse_and_cleanup) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto room1 = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room1);
  if (!room1) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 同一协程内再次 mutable_room 返回同一对象
  CASE_EXPECT_EQ(0, env.run("reuse_room", [team_id, &room1](rpc::context& ctx) -> rpc::result_code_type {
    auto room2 = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id));
    CASE_EXPECT_TRUE(room2.get() == room1.get());
    RPC_RETURN_CODE(0);
  }));

  CASE_EXPECT_EQ(1u, team_room_manager::me()->get_room_count());

  // 相同 team_id、不同 zone_id 是不同队伍，room 与 DTMQ channel 都不能复用。
  auto other_zone_key = make_team_key(team_id, kTestZoneId + 1);
  team_room::ptr_t other_zone_room;
  CASE_EXPECT_EQ(0, env.run("same_id_other_zone",
                            [&other_zone_key, &other_zone_room](rpc::context& ctx) -> rpc::result_code_type {
    other_zone_room = team_room_manager::me()->mutable_room(ctx, other_zone_key);
    CASE_EXPECT_TRUE(!!other_zone_room);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_TRUE(other_zone_room.get() != room1.get());
  if (room1 && other_zone_room) {
    CASE_EXPECT_NE(room1->get_channel_key().channel_id(), other_zone_room->get_channel_key().channel_id());
    CASE_EXPECT_EQ(kTestZoneId + 1, other_zone_room->get_team_key().zone_id());
    CASE_EXPECT_EQ(team_id, other_zone_room->get_team_key().team_id());
  }
  CASE_EXPECT_EQ(2u, team_room_manager::me()->get_room_count());

  // 不同 team id 得到不同 room
  int64_t other_team = next_test_team_id();
  CASE_EXPECT_EQ(0, env.run("another_room", [other_team](rpc::context& ctx) -> rpc::result_code_type {
    auto room3 = team_room_manager::me()->mutable_room(ctx, make_team_key(other_team));
    CASE_EXPECT_TRUE(!!room3);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_EQ(3u, team_room_manager::me()->get_room_count());

  // team_id 为 0 非法;zone_id 为 0 是合法的不分区队伍,与 (kTestZoneId, team_id) 是不同的 room
  team_room::ptr_t global_room;
  CASE_EXPECT_EQ(0, env.run("team_key_boundaries", [team_id, &global_room](rpc::context& ctx) -> rpc::result_code_type {
    CASE_EXPECT_FALSE(!!team_room_manager::me()->mutable_room(ctx, make_team_key(0)));
    global_room = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id, 0));
    CASE_EXPECT_TRUE(!!global_room);
    RPC_RETURN_CODE(0);
  }));
  CASE_EXPECT_TRUE(global_room.get() != room1.get());
  if (global_room && room1) {
    CASE_EXPECT_EQ(0u, global_room->get_team_key().zone_id());
    CASE_EXPECT_EQ(team_id, global_room->get_team_key().team_id());
    CASE_EXPECT_NE(room1->get_channel_key().channel_id(), global_room->get_channel_key().channel_id());
  }
  CASE_EXPECT_EQ(4u, team_room_manager::me()->get_room_count());

  // 外层频道 key 是权威标识；携带错误完整 key 的 action 写入前必须规范化，不能产生跨区日志。
  atfw::team::DTeamAction mismatched_action;
  protobuf_copy_message(*mismatched_action.mutable_destroy_team(), make_team_key(other_team, kTestZoneId + 1));
  CASE_EXPECT_EQ(0, env.run("normalize_embedded_team_key",
                            [&room1, &mismatched_action](rpc::context& ctx) -> rpc::result_code_type {
                              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room1->send_action(ctx, mismatched_action)));
                            }));
  bool found_normalized_destroy = false;
  CASE_EXPECT_TRUE(env.channel(team_id).foreach_team_action(
      [team_id, &found_normalized_destroy](const atfw::dtmq::DChannelMessage&, const atfw::team::DTeamAction& action) {
        if (!action.has_destroy_team()) {
          return true;
        }
        found_normalized_destroy = true;
        CASE_EXPECT_EQ(kTestZoneId, action.destroy_team().zone_id());
        CASE_EXPECT_EQ(team_id, action.destroy_team().team_id());
        return false;
      }));
  CASE_EXPECT_TRUE(found_normalized_destroy);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0u, team_room_manager::me()->get_room_count());
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ INF-04: typed invoke_ss_action 驱动业务 action(heartbeat 直达) ============
CASE_TEST(teamsvr_room_infrastructure, typed_ss_action_invoke) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner_key = make_user_key(1, 2001);
  auto owner_channel = make_personal_channel(2001);

  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner_key, owner_channel, &room));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 通过 typed action helper 驱动 task_action_heartbeat(真实 action 协程，含路由判定)
  atframework::testing::ss_action_invoke_options invoke_options{rpc::team::packer::get_full_name_of_heartbeat()};
  invoke_options.source.node_id = kDtmqProxyNodeId;
  invoke_options.source.node_name = "unit-test-dtmq-proxy";

  size_t send_before = env.channel(team_id).send_message_calls();
  int32_t action_ret = env.run(
      "invoke_heartbeat_action", [&invoke_options, team_id, &owner_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomHeartbeatReq request;
        protobuf_copy_message(*request.mutable_team_key(), make_team_key(team_id));
        protobuf_copy_message(*request.mutable_user_key(), owner_key);
        request.set_user_router_server_id(0x1234);
        request.set_sequence(1);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            atframework::testing::invoke_ss_action<task_action_heartbeat>(ctx, request, invoke_options)));
      });
  CASE_EXPECT_EQ(0, action_ret);

  // 心跳不写频道日志
  CASE_EXPECT_EQ(send_before, env.channel(team_id).send_message_calls());
  // 心跳业务效果: 成员已确认日志序号与通知路由被更新(内存状态，不下发日志)
  auto heartbeat_member = room->find_member(owner_key, false);
  CASE_EXPECT_TRUE(!!heartbeat_member);
  if (heartbeat_member) {
    CASE_EXPECT_EQ(1, heartbeat_member->member_data.acknowledge_action_sequence());
    CASE_EXPECT_EQ(0x1234u, heartbeat_member->member_data.user_router_server_id());
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CRT-01: 显式 team id 创建成功，初始 update save=true，创建者为 OWNER/队长 ============
CASE_TEST(teamsvr_room_create, create_team_success) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner_key = make_user_key(1, 3001);
  auto owner_channel = make_personal_channel(3001);

  // 创建者(首任队长)的版本/路由随 create 上报(与 lobbysvr create_team 的真实行为一致)，
  // 入队后其他成员才能在快照里看到队长的版本信息
  const std::string owner_client_version = "ut-create-v1";
  constexpr uint64_t kOwnerRouterServerId = 0x1234ABCD;

  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner_key, owner_channel, &room, nullptr,
                                           &owner_client_version, kOwnerRouterServerId));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  auto& fake = env.channel(team_id);

  // 创建流程: 订阅 -> (无历史日志) -> 首帧 update 合并锁 CAS(save=true, custom+private)，
  // 不再有独立的 reset_lock 往返
  CASE_EXPECT_TRUE(room->is_lock_holder());
  CASE_EXPECT_EQ(0u, fake.reset_lock_calls());
  CASE_EXPECT_GE(fake.update_calls(), 1u);

  // 初始 update 请求: save=true，携带完整公共/私有初始数据
  const atfw::dtmq::SSChannelUpdateReq* initial_update = nullptr;
  for (const auto& record : fake.update_requests()) {
    if (record.request.save()) {
      initial_update = &record.request;
      break;
    }
  }
  CASE_EXPECT_TRUE(nullptr != initial_update);
  if (nullptr != initial_update) {
    CASE_EXPECT_TRUE(initial_update->save());
    CASE_EXPECT_EQ(0, initial_update->compact_sequence());

    // 合并的锁 CAS: 首帧 update 携带 allow_empty 的检查器，reset 目标即本节点锁
    CASE_EXPECT_TRUE(initial_update->has_compare_and_maybe_reset_lock());
    if (initial_update->has_compare_and_maybe_reset_lock()) {
      const auto& cas = initial_update->compare_and_maybe_reset_lock();
      CASE_EXPECT_TRUE(cas.allow_empty_real_value());
      CASE_EXPECT_TRUE(cas.has_reset_value());
      CASE_EXPECT_FALSE(cas.reset_value().lock_holder().empty());
    }

    atfw::team::DTeamStorage storage;
    CASE_EXPECT_TRUE(initial_update->custom_data().UnpackTo(&storage));
    CASE_EXPECT_EQ(team_id, storage.team_key().team_id());
    CASE_EXPECT_EQ(kTestZoneId, storage.team_key().zone_id());
    CASE_EXPECT_EQ(1, storage.member_size());
    if (1 == storage.member_size()) {
      CASE_EXPECT_EQ(owner_key.user_id(), storage.member(0).user_key().user_id());
      CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, storage.member(0).role());
      // 创建者上报的版本/路由必须落入频道快照(custom_data)，订阅端才能经快照看到队长的版本信息
      CASE_EXPECT_EQ(owner_client_version, storage.member(0).client_version());
      CASE_EXPECT_EQ(kOwnerRouterServerId, storage.member(0).user_router_server_id());
    }
    CASE_EXPECT_EQ(owner_key.user_id(), storage.captain_user_key().user_id());
    // team_type 随 create 落入首帧快照(后续邀请/审批事件与配置默认值均以其为准)
    CASE_EXPECT_EQ(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL), storage.team_type());

    atfw::team::DTeamRoomPrivateData private_data;
    CASE_EXPECT_TRUE(initial_update->private_data().UnpackTo(&private_data));
    CASE_EXPECT_TRUE(private_data.team_created());
  }

  // 房间内成员状态
  auto owner = room->find_member(owner_key, false);
  CASE_EXPECT_TRUE(!!owner);
  if (owner) {
    CASE_EXPECT_EQ(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER, owner->member_data.role());
    CASE_EXPECT_EQ(owner_client_version, owner->member_data.client_version());
    CASE_EXPECT_EQ(kOwnerRouterServerId, owner->member_data.user_router_server_id());
  }
  CASE_EXPECT_EQ(static_cast<uint32_t>(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL), room->get_team_type());

  // 事件回环(初始 update 产生的 kUpdateCustomData/kNoop 日志)后房间仍为主控
  CASE_EXPECT_EQ(0, env.sync(team_id));
  CASE_EXPECT_TRUE(room->is_lock_holder());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CRT-02: team id 为 0 时 UUID 成功后创建 room ============
CASE_TEST(teamsvr_room_create, create_team_with_uuid) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  auto owner_key = make_user_key(1, 3101);
  auto owner_channel = make_personal_channel(3101);

  constexpr uint64_t kCreateSequence = 3101001;
  atframework::testing::ss_action_invoke_options invoke_options{rpc::team::packer::get_full_name_of_create()};
  invoke_options.source.node_id = kDtmqProxyNodeId;
  invoke_options.source.node_name = "unit-test-dtmq-proxy";
  invoke_options.source.sequence = kCreateSequence;

  // 真实 action 输入 team_id=0：由 action 调 UUID，写回生成后的 team_id，再按完整 key 路由并创建房间。
  int32_t action_ret = env.run(
      "create_with_uuid", [&invoke_options, &owner_key, &owner_channel](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomCreateReq request;
        request.mutable_team_key()->set_zone_id(kTestZoneId);
        protobuf_copy_message(*request.mutable_sender_user_key(), owner_key);
        protobuf_copy_message(*request.mutable_sender_user_channel(), owner_channel);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            atframework::testing::invoke_ss_action<task_action_create>(ctx, request, invoke_options)));
      });
  CASE_EXPECT_EQ(0, action_ret);

  const atfw::testing::outbound_message* response_record = nullptr;
  for (size_t i = 0; i < env.runtime().transport().outbound_count(); ++i) {
    const auto* record = env.runtime().transport().outbound_at(i);
    if (record != nullptr && record->target_node_id == kDtmqProxyNodeId && record->sequence == kCreateSequence) {
      response_record = record;
      break;
    }
  }
  CASE_EXPECT_TRUE(response_record != nullptr);
  atfw::team::DTeamKey created_key;
  if (response_record != nullptr) {
    atframework::SSMsg response_message;
    CASE_EXPECT_TRUE(response_message.ParseFromArray(response_record->payload.data(),
                                                     static_cast<int>(response_record->payload.size())));
    CASE_EXPECT_EQ(0, response_message.head().error_code());
    atfw::team::SSTeamRoomCreateRsp response;
    CASE_EXPECT_TRUE(response.ParseFromString(response_message.body_bin()));
    CASE_EXPECT_EQ(0, response.client_result());
    protobuf_copy_message(created_key, response.team_key());
    // CRT-02: 创建响应附带标准化房间频道(make_unicast_channel_id 标准单播格式),与房间实际订阅频道一致
    CASE_EXPECT_EQ(kTeamRoomChannelType, response.room_channel().channel_type());
    CASE_EXPECT_EQ(rpc::dtmq::make_unicast_channel_id(kTeamRoomChannelType, created_key.zone_id(),
                                                      static_cast<uint64_t>(created_key.team_id())),
                   response.room_channel().channel_id());
  }

  CASE_EXPECT_EQ(kTestZoneId, created_key.zone_id());
  CASE_EXPECT_GT(created_key.team_id(), 0);
  auto room = team_room_manager::me()->get_room(created_key);
  CASE_EXPECT_TRUE(!!room);
  if (room) {
    CASE_EXPECT_TRUE(room->is_lock_holder());
    const auto& fake = env.channel(created_key);
    CASE_EXPECT_GE(fake.update_calls(), 1u);
  }

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CRT-03: 非法 sender key、重复创建、已销毁频道返回精确错误且零额外写入 ============
CASE_TEST(teamsvr_room_create, create_team_invalid_states) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner_key = make_user_key(1, 3201);
  auto owner_channel = make_personal_channel(3201);

  team_room::ptr_t room;
  CASE_EXPECT_EQ(0, env.setup_created_team(team_id, owner_key, owner_channel, &room));
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }
  CASE_EXPECT_EQ(0, env.sync(team_id));

  auto& fake = env.channel(team_id);

  // 重复创建: no permission，且不再有权威写入
  size_t updates_before = fake.update_calls();
  size_t sends_before = fake.send_message_calls();
  size_t personal_before = env.personal_message_count();
  int32_t dup_ret =
      env.run("duplicate_create", [room, team_id, &owner_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomCreateReq req;
        protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
        protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
      });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, dup_ret);
  CASE_EXPECT_EQ(updates_before, fake.update_calls());
  CASE_EXPECT_EQ(sends_before, fake.send_message_calls());
  CASE_EXPECT_EQ(personal_before, env.personal_message_count());

  // 非法 sender key: invalid param(参数校验先于任何写入)
  int64_t other_team = next_test_team_id();
  team_room::ptr_t other_room = env.setup_ready_room(other_team);
  CASE_EXPECT_TRUE(!!other_room);
  if (other_room) {
    auto& other_fake = env.channel(other_team);
    size_t other_updates = other_fake.update_calls();
    size_t other_sends = other_fake.send_message_calls();
    size_t other_personal = env.personal_message_count();
    int32_t invalid_ret =
        env.run("invalid_sender", [other_room, other_team](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::SSTeamRoomCreateReq req;
          protobuf_copy_message(*req.mutable_team_key(), make_team_key(other_team));
          auto bad_key = make_user_key(0, 0);
          protobuf_copy_message(*req.mutable_sender_user_key(), bad_key);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(other_room->create_team(ctx, req)));
        });
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, invalid_ret);
    expect_zero_write(other_fake, other_sends, other_updates, 0, 0, env, other_personal);
  }

  // 已销毁队伍: 队长写 destroy_team -> 事件回环 -> 再 create 返回 destroyed
  atfw::team::DTeamAction destroy_action;
  protobuf_copy_message(*destroy_action.mutable_destroy_team(), make_team_key(team_id));
  int32_t destroy_ret = env.run("destroy_team", [room, &destroy_action](rpc::context& ctx) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->send_action(ctx, destroy_action)));
  });
  CASE_EXPECT_EQ(0, destroy_ret);
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 已销毁后不再有任何写入(含个人频道)
  size_t destroyed_sends = fake.send_message_calls();
  size_t destroyed_updates = fake.update_calls();
  size_t destroyed_resets = fake.reset_lock_calls();
  size_t destroyed_destroys = fake.destroy_calls();
  size_t destroyed_personal = env.personal_message_count();
  int32_t destroyed_ret =
      env.run("create_on_destroyed", [room, team_id, &owner_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomCreateReq req;
        protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
        protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
      });
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED, destroyed_ret);
  expect_zero_write(fake, destroyed_sends, destroyed_updates, destroyed_resets, destroyed_destroys, env,
                    destroyed_personal);

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ CRT-04: 创建 update 已提交但响应丢失；重新订阅后从已提交快照恢复，不覆盖第二支初始状态 ============
CASE_TEST(teamsvr_room_create, create_response_loss_idempotent) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  int64_t team_id = next_test_team_id();
  auto owner_key = make_user_key(1, 3401);

  team_room::ptr_t room = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!room);
  if (!room) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 一次性故障: 提交 update(镜像服务端)后以错误响应(响应丢失/传输失败)
  auto& fault_channel = env.channel(team_id);
  fault_channel.next_update_fault.present = true;
  fault_channel.next_update_fault.commit_first = true;
  fault_channel.next_update_fault.error_code = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;

  int32_t first_ret =
      env.run("create_lost_response", [room, team_id, &owner_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomCreateReq req;
        protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
        protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
      });
  // 响应丢失: create_team 返回非零
  CASE_EXPECT_NE(0, first_ret);

  auto& fake = env.channel(team_id);
  CASE_EXPECT_EQ(1u, fake.update_calls());
  CASE_EXPECT_FALSE(fake.custom_data().type_url().empty());

  // 事件回环: 已提交的初始快照对订阅者可见
  CASE_EXPECT_EQ(0, env.sync(team_id));

  // 丢弃原 room，重新订阅恢复: 从已提交快照看到 team_created -> create 返回 no permission，
  // 且不产生第二支初始 update
  room_test_env::clear_rooms();
  team_room::ptr_t recovered = env.setup_ready_room(team_id);
  CASE_EXPECT_TRUE(!!recovered);
  if (recovered) {
    int32_t retry_ret =
        env.run("create_retry", [recovered, team_id, &owner_key](rpc::context& ctx) -> rpc::result_code_type {
          atfw::team::SSTeamRoomCreateReq req;
          protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
          protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(recovered->create_team(ctx, req)));
        });
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION, retry_ret);
  }
  CASE_EXPECT_EQ(1u, fake.update_calls());

  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ROU-01: 本地一致性哈希目标在本节点执行 ============
CASE_TEST(teamsvr_room_routing, local_hash_target) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // 注入的本地 teamsvr-room 节点是唯一 room 节点: 一致性哈希全部解析到本节点
  for (int64_t probe : {1LL, 42LL, 0x7FFFFFFFLL}) {
    CASE_EXPECT_EQ(kLocalRoomNodeId, rpc::team::team_api::get_teamsvr_room_server_id_of_zone(make_team_key(probe)));
  }

  CASE_EXPECT_EQ(0, env.stop());
}

// ============ ROU-02: 远端目标仅转发，不在本节点创建 room ============
CASE_TEST(teamsvr_room_routing, forward_to_remote) {
  room_test_env env;
  if (!env.start()) {
    return;
  }

  // 注入第二个 teamsvr-room 节点，找一个哈希到远端的 team id
  constexpr uint64_t kRemoteRoomNodeId = 0x11000002;
  {
    atframework::testing::mock_node node;
    node.set_id(kRemoteRoomNodeId)
        .set_name("unit-test-teamsvr-room-remote")
        .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kTeamRoomSvr))
        .set_type_name("teamsvr-room")
        .set_zone_id(1)
        .add_label("hpa_scaling_ready", "1")
        .add_label("hpa_scaling_target", "1");
    CASE_EXPECT_TRUE(!!env.runtime().discovery().add_node(node));
    if (nullptr != logic_server_last_common_module()) {
      logic_server_last_common_module()->reload();
    }
  }

  int64_t remote_team_id = 0;
  for (int64_t i = 1; i <= 4096 && 0 == remote_team_id; ++i) {
    int64_t candidate = 0x2000000 + i;
    if (rpc::team::team_api::get_teamsvr_room_server_id_of_zone(make_team_key(candidate)) == kRemoteRoomNodeId) {
      remote_team_id = candidate;
    }
  }
  CASE_EXPECT_NE(0, remote_team_id);
  if (0 == remote_team_id) {
    CASE_EXPECT_EQ(0, env.stop());
    return;
  }

  // 远端节点的 create 转发: 注册 TeamRoomService/create 规则模拟远端执行
  auto remote_rule = env.runtime().ss().mock(
      rpc::team::packer::get_full_name_of_create(), atfw::team::SSTeamRoomCreateReq::descriptor()->full_name(),
      atfw::team::SSTeamRoomCreateRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const atfw::team::SSTeamRoomCreateReq&>(request.body);
        auto& typed_response = static_cast<atfw::team::SSTeamRoomCreateRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_team_key(), typed_request.team_key());
        typed_response.set_client_result(0);
        RPC_RETURN_CODE(0);
      },
      atframework::testing::ss_rule_options{});
  CASE_EXPECT_TRUE(!!remote_rule);

  atframework::testing::ss_action_invoke_options invoke_options{rpc::team::packer::get_full_name_of_create()};
  invoke_options.source.node_id = kDtmqProxyNodeId;

  int32_t action_ret =
      env.run("create_forwarded", [&invoke_options, remote_team_id](rpc::context& ctx) -> rpc::result_code_type {
        atfw::team::SSTeamRoomCreateReq request;
        protobuf_copy_message(*request.mutable_team_key(), make_team_key(remote_team_id));
        auto key = make_user_key(1, 3501);
        protobuf_copy_message(*request.mutable_sender_user_key(), key);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            atframework::testing::invoke_ss_action<task_action_create>(ctx, request, invoke_options)));
      });
  CASE_EXPECT_EQ(0, action_ret);

  // 转发路径不在本节点创建 room
  CASE_EXPECT_EQ(nullptr, team_room_manager::me()->get_room(make_team_key(remote_team_id)).get());

  remote_rule.reset();
  room_test_env::clear_rooms();
  CASE_EXPECT_EQ(0, env.stop());
}
