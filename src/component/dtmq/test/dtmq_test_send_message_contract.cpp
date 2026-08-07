// Copyright 2026 atframework

// Sampling contract test for the dtmq client SDK SS consumer path (IMPLEMENTATION_PLAN.md 阶段 9):
// rpc::dtmq::send_message (client API) resolves the dtmq-proxysvr node by consistent hash over the real
// discovery index and issues atframework.dtmq.DtmqProxysvrService/send_message; the SS mock engine
// answers it offline.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <utility>

#include "config/extern_service_types.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"
#include "rpc/dtmq/dtmq_client_api.h"

CASE_TEST(component_dtmq, dtmq_client_send_message_contract) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  // The only dtmq-proxysvr node: consistent hash must select it. The HPA-patched scaling_ready selector
  // requires the hpa_scaling_ready=1 metadata label (see logic_hpa_controller and the rank sample).
  atframework::testing::mock_node node;
  node.set_id(0x1C0001)
      .set_name("unit-test-dtmq-proxy")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtMqProxySvr))
      .set_type_name("dtmq-proxysvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  // Mock injection writes the global discovery set directly; the common-module discovery index only
  // replays existing nodes on reload (node events fire only from the etcd watch path).
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  auto rule = test.ss().mock("atframework.dtmq.DtmqProxysvrService/send_message",
                             atfw::dtmq::SSChannelSendMessageReq::descriptor()->full_name(),
                             atfw::dtmq::SSChannelSendMessageRsp::descriptor()->full_name(),
                             [](const atframework::testing::ss_request_view &request,
                                google::protobuf::Message &response) -> rpc::result_code_type {
                               const auto &typed_request =
                                   static_cast<const atfw::dtmq::SSChannelSendMessageReq &>(request.body);
                               CASE_EXPECT_EQ(0x1C0001, static_cast<int64_t>(request.target_node_id));
                               CASE_EXPECT_EQ("chan-unit-test", typed_request.channel_key().channel_id());
                               CASE_EXPECT_EQ("hello-dtmq", typed_request.message_content().detail().text());
                               CASE_EXPECT_EQ("U:1:10001", typed_request.subscriber().subscriber_key());
                               CASE_EXPECT_TRUE(typed_request.auto_create_channel());
                               static_cast<atfw::dtmq::SSChannelSendMessageRsp &>(response).set_client_result(0);
                               RPC_RETURN_CODE(0);
                             });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.ss().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task =
      test.run_task("dtmq_send_message", std::chrono::seconds{3}, [](rpc::context &ctx) -> rpc::result_code_type {
        atfw::dtmq::channel_subscriber sender;
        sender.set_subscriber_server_id(0x11000001);
        sender.set_subscriber_key("U:1:10001");
        atfw::dtmq::DChannelIdKey channel_key;
        channel_key.set_channel_id("chan-unit-test");
        channel_key.set_channel_type(0);
        atfw::dtmq::DChannelMessageDetail detail;
        detail.set_text("hello-dtmq");

        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, std::move(sender), channel_key,
                                                                    std::move(detail), nullptr, nullptr, true, false));
        CASE_EXPECT_EQ(0, res);
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls("atframework.dtmq.DtmqProxysvrService/send_message")));

  CASE_EXPECT_EQ(0, test.stop());
}
