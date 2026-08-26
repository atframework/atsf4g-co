// Copyright 2026 atframework

// Verifies task_action_subscribe's not_found_channel_ids contract for non-auto_create subscriptions:
//   - non-auto_create subscribe for a channel that has never been created locally (writable owner,
//     memory-only type) -> not_found, and the channel must NOT be implicitly created
//   - auto_create subscribe for the same setup -> channel created and subscribe_node echoed (contrast)
//   - non-auto_create subscribe for a DB-backed channel with no DB record -> not_found
//   - non-auto_create subscribe for a DB-backed channel with an existing DB record -> restored and
//     subscribe_node echoed (restore keeps working)
//   - non-auto_create subscribe landing on a local readonly replica whose writable node reports
//     not_found -> the not_found is propagated to the subscriber
//
// The action response is not captured by mock_ss (history keeps only the head), so these cases read the
// full outbound payload from raw_transport and parse SSChannelSubscribeRsp from it.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/raw_transport.h>
#include <atframework/testing/ss_action.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "logic/action/task_action_subscribe.h"
#include "rpc/rpc_async_invoke.h"

#include "dtmq_test_channel_common.h"  // NOLINT(build/include_subdir)

using namespace dtmq_channel_test;  // NOLINT(build/namespaces)

namespace {

constexpr uint64_t kSubscribeSourceNodeId = kPeerNode3;
constexpr uint64_t kSubscribeSourceTaskId = 0x5B01;
constexpr uint64_t kSubscribeSourceSequence = 0x5B02;

atframework::testing::ss_action_invoke_options make_subscribe_action_options() {
  atframework::testing::ss_action_invoke_options result{rpc::dtmq::packer::get_full_name_of_subscribe()};
  result.source.node_id = kSubscribeSourceNodeId;
  result.source.node_name = "dtmq-subscribe-source";
  result.source.source_task_id = kSubscribeSourceTaskId;
  result.source.sequence = kSubscribeSourceSequence;
  return result;
}

atfw::dtmq::SSChannelSubscribeReq make_subscribe_request(const atfw::dtmq::DChannelIdKey& channel_key,
                                                         bool auto_create_channel, uint64_t readonly_index) {
  atfw::dtmq::SSChannelSubscribeReq request;
  auto* subscriber = request.mutable_subscriber();
  subscriber->set_subscriber_server_id(kSubscribeSourceNodeId);
  subscriber->set_subscriber_key("UT:task-subscribe");
  auto* heartbeat = request.add_heartbeat();
  heartbeat->mutable_channel_key()->CopyFrom(channel_key);
  heartbeat->set_auto_create_channel(auto_create_channel);
  heartbeat->set_readonly_index(readonly_index);
  heartbeat->set_last_sequence(0);
  heartbeat->set_last_hash_code(0);
  return request;
}

// Find the action's response in the raw transport outbound history and parse the response body.
// Returns false when no subscribe response was sent to the source node.
bool find_subscribe_response(atframework::testing::runtime& test, atfw::dtmq::SSChannelSubscribeRsp& rsp_body,
                             atfw::SSMsgHead& rsp_head) {
  const std::string subscribe_rpc_name{rpc::dtmq::packer::get_full_name_of_subscribe()};
  for (size_t index = 0; index < test.transport().outbound_count(); ++index) {
    const auto* record = test.transport().outbound_at(index);
    if (nullptr == record || record->target_node_id != kSubscribeSourceNodeId) {
      continue;
    }
    if (record->type != static_cast<int32_t>(atfw::component::message_type::kInServerMessage)) {
      continue;
    }

    atfw::SSMsg msg;
    if (!msg.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size()))) {
      continue;
    }
    if (!msg.head().has_rpc_response() || msg.head().rpc_response().rpc_name() != subscribe_rpc_name) {
      continue;
    }
    if (!rsp_body.ParseFromString(msg.body_bin())) {
      continue;
    }
    rsp_head.CopyFrom(msg.head());
    return true;
  }
  return false;
}

void expect_channel_not_created(const std::string& channel_id) {
  auto channel = mq_channel_manager::me()->get_channel(channel_id);
  // 允许存在临时频道对象（等待过期清理），但它一定不能进入可用状态
  CASE_EXPECT_TRUE(!channel || !channel->is_available());
}

}  // namespace

// 非 auto_create 订阅本地未创建的纯内存频道（writable 属主在本机）：稳定下发 not_found，且不隐式创建频道
CASE_TEST(component_dtmq_task_subscribe, non_auto_create_missing_channel_returns_not_found) {
  atframework::testing::runtime test;
  if (!start_dtmq_proxysvr_runtime(test, 2, true, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("sub-nf-mo", local_id);
  CASE_EXPECT_FALSE(channel_id.empty());
  if (channel_id.empty()) {
    test.stop();
    return;
  }

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestChannelType);

  auto task =
      test.run_task("sub_nf_mo", std::chrono::seconds{4}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        int32_t result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_subscribe>(
            ctx, make_subscribe_request(channel_key, false, 0), make_subscribe_action_options()));
        CASE_EXPECT_EQ(0, result);
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  atfw::dtmq::SSChannelSubscribeRsp rsp_body;
  atfw::SSMsgHead rsp_head;
  CASE_EXPECT_TRUE(find_subscribe_response(test, rsp_body, rsp_head));
  CASE_EXPECT_EQ(0, rsp_head.error_code());
  CASE_EXPECT_EQ(1, rsp_body.not_found_channel_ids_size());
  if (rsp_body.not_found_channel_ids_size() > 0) {
    CASE_EXPECT_EQ(channel_id, rsp_body.not_found_channel_ids(0));
  }
  CASE_EXPECT_EQ(0, rsp_body.subscribe_node_size());

  expect_channel_not_created(channel_id);
  CASE_EXPECT_EQ(0, test.stop());
}

// 对照组：auto_create 订阅同样的本地未创建频道，频道被创建且回 subscribe_node
CASE_TEST(component_dtmq_task_subscribe, auto_create_missing_channel_creates_and_subscribes) {
  atframework::testing::runtime test;
  if (!start_dtmq_proxysvr_runtime(test, 2, true, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("sub-create-mo", local_id);
  CASE_EXPECT_FALSE(channel_id.empty());
  if (channel_id.empty()) {
    test.stop();
    return;
  }

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestChannelType);

  auto task = test.run_task(
      "sub_create_mo", std::chrono::seconds{4}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        int32_t result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_subscribe>(
            ctx, make_subscribe_request(channel_key, true, 0), make_subscribe_action_options()));
        CASE_EXPECT_EQ(0, result);
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  atfw::dtmq::SSChannelSubscribeRsp rsp_body;
  atfw::SSMsgHead rsp_head;
  CASE_EXPECT_TRUE(find_subscribe_response(test, rsp_body, rsp_head));
  CASE_EXPECT_EQ(0, rsp_head.error_code());
  CASE_EXPECT_EQ(0, rsp_body.not_found_channel_ids_size());
  CASE_EXPECT_EQ(1, rsp_body.subscribe_node_size());
  if (rsp_body.subscribe_node_size() > 0) {
    CASE_EXPECT_EQ(channel_id, rsp_body.subscribe_node(0).channel_key().channel_id());
    CASE_EXPECT_EQ(local_id, rsp_body.subscribe_node(0).server_id());
  }

  auto channel = mq_channel_manager::me()->get_channel(channel_id);
  CASE_EXPECT_TRUE(!!channel && channel->is_writable());
  CASE_EXPECT_EQ(0, test.stop());
}

// 非 auto_create 订阅本地未创建的 DB 频道（无 DB 记录）：稳定下发 not_found，且不从空记录隐式创建
CASE_TEST(component_dtmq_task_subscribe, non_auto_create_db_backed_missing_record_returns_not_found) {
  atframework::testing::runtime test;
  if (!start_dtmq_proxysvr_runtime(test, 2, false, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("sub-nf-db", local_id, 200, kTestDbBackedChannelType);
  CASE_EXPECT_FALSE(channel_id.empty());
  if (channel_id.empty()) {
    test.stop();
    return;
  }

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestDbBackedChannelType);

  auto task =
      test.run_task("sub_nf_db", std::chrono::seconds{4}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        int32_t result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_subscribe>(
            ctx, make_subscribe_request(channel_key, false, 0), make_subscribe_action_options()));
        CASE_EXPECT_EQ(0, result);
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  atfw::dtmq::SSChannelSubscribeRsp rsp_body;
  atfw::SSMsgHead rsp_head;
  CASE_EXPECT_TRUE(find_subscribe_response(test, rsp_body, rsp_head));
  CASE_EXPECT_EQ(0, rsp_head.error_code());
  CASE_EXPECT_EQ(1, rsp_body.not_found_channel_ids_size());
  if (rsp_body.not_found_channel_ids_size() > 0) {
    CASE_EXPECT_EQ(channel_id, rsp_body.not_found_channel_ids(0));
  }
  CASE_EXPECT_EQ(0, rsp_body.subscribe_node_size());

  expect_channel_not_created(channel_id);
  CASE_EXPECT_EQ(0, test.stop());
}

// 非 auto_create 订阅 DB 中已存在的频道：从 DB 恢复并回 subscribe_node（恢复流程不被误伤）
CASE_TEST(component_dtmq_task_subscribe, non_auto_create_db_backed_existing_record_restores) {
  atframework::testing::runtime test;
  if (!start_dtmq_proxysvr_runtime(test, 2, false, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_local_writable_channel_id("sub-restore-db", local_id, 200, kTestDbBackedChannelType);
  CASE_EXPECT_FALSE(channel_id.empty());
  if (channel_id.empty()) {
    test.stop();
    return;
  }

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestDbBackedChannelType);

  // 先通过真实流程创建并保存一条 DB 记录（replace 无 mock 规则时落入内存后端），然后清空本地缓存，
  // 模拟"频道存在于 DB 但本地未创建"的场景
  auto prepare_task = test.run_task(
      "sub_restore_db_prepare", std::chrono::seconds{4}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        mq_channel_manager::mq_channel_ptr_type channel;
        uint64_t forward_server_id = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(mq_channel_manager::me()->make_writable_channel(
                              ctx, channel, forward_server_id, channel_key, true)));
        CASE_EXPECT_TRUE(!!channel && channel->is_writable());
        if (!channel) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
        }

        int32_t wal_result = 0;
        mq_channel_wal_object_context wal_context{ctx, wal_result};
        auto message = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                                 atfw::dtmq::DChannelMessageDetail::kText, wal_context);
        CASE_EXPECT_TRUE(!!message);
        if (!message) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
        message->mutable_detail()->set_text("restore-payload");
        channel->get_wal_publisher().emplace_back_log(std::move(message), wal_context);

        channel->set_dirty();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(channel->save(ctx)));
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(prepare_task.empty());
  if (prepare_task.empty()) {
    test.stop();
    return;
  }
  {
    auto result = test.wait(prepare_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    if (!result.task_exited || 0 != result.result_code) {
      test.stop();
      return;
    }
  }
  clear_manager_for_unit_test();
  CASE_EXPECT_FALSE(!!mq_channel_manager::me()->get_channel(channel_id));

  auto task = test.run_task(
      "sub_restore_db", std::chrono::seconds{4}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        int32_t result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_subscribe>(
            ctx, make_subscribe_request(channel_key, false, 0), make_subscribe_action_options()));
        CASE_EXPECT_EQ(0, result);
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  atfw::dtmq::SSChannelSubscribeRsp rsp_body;
  atfw::SSMsgHead rsp_head;
  CASE_EXPECT_TRUE(find_subscribe_response(test, rsp_body, rsp_head));
  CASE_EXPECT_EQ(0, rsp_head.error_code());
  CASE_EXPECT_EQ(0, rsp_body.not_found_channel_ids_size());
  CASE_EXPECT_EQ(1, rsp_body.subscribe_node_size());
  if (rsp_body.subscribe_node_size() > 0) {
    CASE_EXPECT_EQ(channel_id, rsp_body.subscribe_node(0).channel_key().channel_id());
    CASE_EXPECT_EQ(local_id, rsp_body.subscribe_node(0).server_id());
  }

  auto channel = mq_channel_manager::me()->get_channel(channel_id);
  CASE_EXPECT_TRUE(!!channel && channel->is_available());
  CASE_EXPECT_EQ(0, test.stop());
}

// 非 auto_create 订阅落在本地只读副本上，且 writable 节点回复 not_found：not_found 被透传给订阅者
CASE_TEST(component_dtmq_task_subscribe, non_auto_create_readonly_replica_propagates_writable_not_found) {
  atframework::testing::runtime test;
  if (!start_dtmq_proxysvr_runtime(test, 2, true, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  uint64_t readonly_index = 0;
  auto channel_id = find_local_readonly_channel_id("sub-nf-replica", local_id, readonly_index);
  CASE_EXPECT_FALSE(channel_id.empty());
  CASE_EXPECT_NE(0u, readonly_index);
  if (channel_id.empty() || 0 == readonly_index) {
    test.stop();
    return;
  }

  // writable 侧（对端节点）回复 not_found，模拟 writable 节点从未创建该频道
  mutable_readonly_subscribe_mock_mode().store(readonly_subscribe_mock_mode::kNotFound);

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestChannelType);

  auto task = test.run_task(
      "sub_nf_replica", std::chrono::seconds{4},
      [channel_key, readonly_index](rpc::context& ctx) -> rpc::result_code_type {
        int32_t result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_subscribe>(
            ctx, make_subscribe_request(channel_key, false, readonly_index), make_subscribe_action_options()));
        CASE_EXPECT_EQ(0, result);
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  atfw::dtmq::SSChannelSubscribeRsp rsp_body;
  atfw::SSMsgHead rsp_head;
  CASE_EXPECT_TRUE(find_subscribe_response(test, rsp_body, rsp_head));
  CASE_EXPECT_EQ(0, rsp_head.error_code());
  CASE_EXPECT_EQ(1, rsp_body.not_found_channel_ids_size());
  if (rsp_body.not_found_channel_ids_size() > 0) {
    CASE_EXPECT_EQ(channel_id, rsp_body.not_found_channel_ids(0));
  }
  CASE_EXPECT_EQ(0, rsp_body.subscribe_node_size());

  expect_channel_not_created(channel_id);
  CASE_EXPECT_EQ(0, test.stop());
}
