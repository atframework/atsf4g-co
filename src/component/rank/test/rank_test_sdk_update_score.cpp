// Copyright 2026 atframework

// Sampling contract test for the rank SDK SS consumer path (IMPLEMENTATION_PLAN.md 阶段 8):
// rank_api::update_score resolves the rank-board node by consistent hash over the real discovery index
// and issues hello.RankBoardService/rank_set_score; the SS mock engine answers it offline.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/pbdesc/rank_board_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include "config/extern_service_types.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"
#include "rpc/rank_board/rank.h"

CASE_TEST(component_rank, rank_sdk_update_score_contract) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  // The only rank-board node: consistent hash must select it. The HPA controller patches the ranksvr
  // discovery selector with the hpa_scaling_ready=1 label (see logic_hpa_controller), so rank sharding
  // filters nodes by that metadata; the mock node must carry it.
  atframework::testing::mock_node node;
  node.set_id(0x1A0001)
      .set_name("unit-test-rank-board")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kRankBoardSvr))
      .set_type_name("rank-board-svr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  // Consumers of the common-module discovery index (type index, consistent hash) only see injected
  // nodes after a reload: node events fire from the etcd watch path, while mock injection writes the
  // global discovery set directly. A reload replays the existing nodes into the index.
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  auto rule = test.ss().mock(
      PROJECT_NAMESPACE ".RankBoardService/rank_set_score",
      PROJECT_NAMESPACE_ID::SSRankSetScoreReq::descriptor()->full_name(),
      PROJECT_NAMESPACE_ID::SSRankSetScoreRsp::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view &request, google::protobuf::Message &response)
          -> rpc::result_code_type {
        const auto &typed_request = static_cast<const PROJECT_NAMESPACE_ID::SSRankSetScoreReq &>(request.body);
        CASE_EXPECT_EQ(0x1A0001, static_cast<int64_t>(request.target_node_id));
        CASE_EXPECT_EQ(11, static_cast<int>(typed_request.rank_key().rank_type()));
        CASE_EXPECT_EQ(22, static_cast<int>(typed_request.rank_key().rank_instance_id()));
        CASE_EXPECT_EQ(10001, static_cast<int64_t>(typed_request.data().user_key().user_id()));
        CASE_EXPECT_EQ(42, static_cast<int64_t>(typed_request.data().score()));
        CASE_EXPECT_EQ(2, static_cast<int>(typed_request.data().custom_data().sort_fields_size()));
        static_cast<PROJECT_NAMESPACE_ID::SSRankSetScoreRsp &>(response).set_result(0);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.ss().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task = test.run_task(
      "rank_update_score", std::chrono::seconds{3}, [](rpc::context &ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::DRankUserKey user;
        user.set_user_id(10001);
        user.set_zone_id(1);
        PROJECT_NAMESPACE_ID::DRankKey rank;
        rank.set_rank_type(11);
        rank.set_rank_instance_id(22);
        rank.set_sub_rank_type(0);
        rank.set_sub_rank_instance_id(0);
        PROJECT_NAMESPACE_ID::DRankCustomData custom_data;
        custom_data.add_sort_fields(100);
        custom_data.add_sort_fields(200);

        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::rank_board::update_score(ctx, user, rank, 42, custom_data));
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

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(PROJECT_NAMESPACE ".RankBoardService/rank_set_score")));

  CASE_EXPECT_EQ(0, test.stop());
}
