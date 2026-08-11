// Copyright 2026 atframework

// Verifies every dtmq-proxysvr action that routes through task_action_ss_req_base::forward_rpc.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_ss.h>
#include <atframework/testing/ss_action.h>

#include <chrono>
#include <cstdint>
#include <utility>

#include "logic/action/task_action_destroy_channel.h"
#include "logic/action/task_action_find_message.h"
#include "logic/action/task_action_page_query_message.h"
#include "logic/action/task_action_pull.h"
#include "logic/action/task_action_reset_lock.h"
#include "logic/action/task_action_send_message.h"
#include "logic/action/task_action_update.h"
#include "rpc/rpc_async_invoke.h"

#include "dtmq_test_channel_common.h"  // NOLINT(build/include_subdir)

using namespace dtmq_channel_test;  // NOLINT(build/namespaces)

namespace {

constexpr uint64_t kForwardSourceNodeId = kPeerNode3;
constexpr uint64_t kForwardSourceTaskId = 0x5A01;
constexpr uint64_t kForwardSourceSequence = 0x5A02;

atframework::testing::ss_action_invoke_options make_forward_action_options(gsl::string_view rpc_name) {
  atframework::testing::ss_action_invoke_options result{rpc_name};
  result.source.node_id = kForwardSourceNodeId;
  result.source.node_name = "dtmq-forward-source";
  result.source.source_task_id = kForwardSourceTaskId;
  result.source.sequence = kForwardSourceSequence;
  return result;
}

template <class RequestType>
void set_forward_channel(RequestType& request, const atfw::dtmq::DChannelIdKey& channel_key) {
  request.mutable_channel_key()->CopyFrom(channel_key);
  request.set_auto_create_channel(true);
}

}  // namespace

CASE_TEST(component_dtmq_task_forward, all_forward_rpc_actions_preserve_request_and_source) {
  atframework::testing::runtime test;
  if (!start_dtmq_proxysvr_runtime(test, 2, true, 3)) {
    test.stop();
    return;
  }

  auto local_id = logic_config::me()->get_local_server_id();
  auto channel_id = find_remote_readable_channel_id("task-forward", local_id);
  CASE_EXPECT_FALSE(channel_id.empty());
  if (channel_id.empty()) {
    test.stop();
    return;
  }

  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_id(channel_id);
  channel_key.set_channel_type(kTestChannelType);

  size_t handled_calls = 0;
  auto make_handler = [&handled_calls, &channel_id](size_t call_index, auto verify_request) {
    return [&handled_calls, &channel_id, call_index, verify_request = std::move(verify_request)](
               rpc::context& /*ctx*/, const auto& request, auto& /*response*/) -> rpc::result_code_type {
      CASE_EXPECT_EQ(channel_id, request.channel_key().channel_id());
      verify_request(request);
      ++handled_calls;
      CASE_EXPECT_EQ(call_index, handled_calls);
      RPC_RETURN_CODE(0);
    };
  };

  auto send_message_rule =
      rpc::dtmq::mock::send_message(make_handler(1, [](const atfw::dtmq::SSChannelSendMessageReq& request) {
        CASE_EXPECT_TRUE(request.auto_create_channel());
        CASE_EXPECT_EQ("forward-send", request.message_content().detail().text());
      }));
  auto update_rule = rpc::dtmq::mock::update(make_handler(2, [](const atfw::dtmq::SSChannelUpdateReq& request) {
    CASE_EXPECT_TRUE(request.auto_create_channel());
    CASE_EXPECT_TRUE(request.save());
    CASE_EXPECT_EQ(102, request.compact_sequence());
  }));
  auto reset_lock_rule =
      rpc::dtmq::mock::reset_lock(make_handler(3, [](const atfw::dtmq::SSChannelResetLockReq& request) {
        CASE_EXPECT_TRUE(request.auto_create_channel());
        CASE_EXPECT_TRUE(request.compare_and_maybe_reset_lock().allow_empty_real_value());
      }));
  auto destroy_channel_rule =
      rpc::dtmq::mock::destroy_channel(make_handler(4, [](const atfw::dtmq::SSChannelDestroyChannelReq& request) {
        CASE_EXPECT_TRUE(request.compare_and_maybe_reset_lock().ignore_expect_value());
      }));
  auto find_message_rule =
      rpc::dtmq::mock::find_message(make_handler(5, [](const atfw::dtmq::SSChannelFindMessageReq& request) {
        CASE_EXPECT_TRUE(request.auto_create_channel());
        CASE_EXPECT_EQ(101, request.sequence());
      }));
  auto page_query_message_rule =
      rpc::dtmq::mock::page_query_message(make_handler(6, [](const atfw::dtmq::SSChannelQueryMessageReq& request) {
        CASE_EXPECT_TRUE(request.auto_create_channel());
        CASE_EXPECT_EQ(10, request.page_info().page_size());
      }));
  auto pull_rule = rpc::dtmq::mock::pull(make_handler(7, [](const atfw::dtmq::SSChannelPullReq& request) {
    CASE_EXPECT_TRUE(request.auto_create_channel());
    CASE_EXPECT_TRUE(request.need_snapshot());
  }));
  CASE_EXPECT_TRUE(!!send_message_rule);
  CASE_EXPECT_TRUE(!!update_rule);
  CASE_EXPECT_TRUE(!!reset_lock_rule);
  CASE_EXPECT_TRUE(!!destroy_channel_rule);
  CASE_EXPECT_TRUE(!!find_message_rule);
  CASE_EXPECT_TRUE(!!page_query_message_rule);
  CASE_EXPECT_TRUE(!!pull_rule);
  if (!send_message_rule || !update_rule || !reset_lock_rule || !destroy_channel_rule || !find_message_rule ||
      !page_query_message_rule || !pull_rule) {
    test.stop();
    return;
  }

  auto task = test.run_task(
      "dtmq_forward_actions", std::chrono::seconds{6}, [channel_key](rpc::context& ctx) -> rpc::result_code_type {
        atfw::dtmq::SSChannelSendMessageReq send_message_request;
        set_forward_channel(send_message_request, channel_key);
        send_message_request.mutable_message_content()->mutable_detail()->set_text("forward-send");
        int32_t result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_send_message>(
            ctx, send_message_request,
            make_forward_action_options(rpc::dtmq::packer::get_full_name_of_send_message())));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }

        atfw::dtmq::SSChannelUpdateReq update_request;
        set_forward_channel(update_request, channel_key);
        update_request.set_save(true);
        update_request.set_compact_sequence(102);
        result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_update>(
            ctx, update_request, make_forward_action_options(rpc::dtmq::packer::get_full_name_of_update())));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }

        atfw::dtmq::SSChannelResetLockReq reset_lock_request;
        set_forward_channel(reset_lock_request, channel_key);
        reset_lock_request.mutable_compare_and_maybe_reset_lock()->set_allow_empty_real_value(true);
        result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_reset_lock>(
            ctx, reset_lock_request, make_forward_action_options(rpc::dtmq::packer::get_full_name_of_reset_lock())));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }

        atfw::dtmq::SSChannelDestroyChannelReq destroy_channel_request;
        destroy_channel_request.mutable_channel_key()->CopyFrom(channel_key);
        destroy_channel_request.mutable_compare_and_maybe_reset_lock()->set_ignore_expect_value(true);
        result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_destroy_channel>(
            ctx, destroy_channel_request,
            make_forward_action_options(rpc::dtmq::packer::get_full_name_of_destroy_channel())));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }

        atfw::dtmq::SSChannelFindMessageReq find_message_request;
        set_forward_channel(find_message_request, channel_key);
        find_message_request.set_sequence(101);
        result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_find_message>(
            ctx, find_message_request,
            make_forward_action_options(rpc::dtmq::packer::get_full_name_of_find_message())));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }

        atfw::dtmq::SSChannelQueryMessageReq page_query_request;
        set_forward_channel(page_query_request, channel_key);
        page_query_request.mutable_page_info()->set_page_size(10);
        result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_page_query_message>(
            ctx, page_query_request,
            make_forward_action_options(rpc::dtmq::packer::get_full_name_of_page_query_message())));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }

        atfw::dtmq::SSChannelPullReq pull_request;
        set_forward_channel(pull_request, channel_key);
        pull_request.set_need_snapshot(true);
        result = RPC_AWAIT_CODE_RESULT(atframework::testing::invoke_ss_action<task_action_pull>(
            ctx, pull_request, make_forward_action_options(rpc::dtmq::packer::get_full_name_of_pull())));
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);
  test.pump_once();
  test.pump_once();

  CASE_EXPECT_EQ(7u, handled_calls);
  CASE_EXPECT_EQ(7u, test.ss().calls());
  for (size_t index = 0; index < test.ss().calls(); ++index) {
    const auto* call = test.ss().call_at(index);
    CASE_EXPECT_TRUE(nullptr != call);
    if (nullptr == call) {
      continue;
    }
    CASE_EXPECT_TRUE(call->matched_rule);
    CASE_EXPECT_NE(0u, call->target_node_id);
    CASE_EXPECT_NE(local_id, call->target_node_id);
    CASE_EXPECT_TRUE(call->head.has_rpc_forward());
    if (call->head.has_rpc_forward()) {
      CASE_EXPECT_TRUE(call->head.rpc_forward().transparent());
      CASE_EXPECT_EQ(kForwardSourceNodeId, call->head.rpc_forward().forward_for_node_id());
      CASE_EXPECT_EQ(kForwardSourceTaskId, call->head.rpc_forward().forward_for_source_task_id());
      CASE_EXPECT_EQ(kForwardSourceSequence, call->head.rpc_forward().forward_for_sequence());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}
