// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/compliance_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>

#include <config/extern_service_types.h>
#include <frame/test_macros.h>
#include <logic/logic_server_setup.h>
#include <rpc/rpc_shared_message.h>

#include <chrono>
#include <cstdint>

#include "rpc/compliance/compliance_client_api.h"

namespace {
static atfw::testing::mock_node make_compliance_node(uint64_t node_id, const char* name, bool ready) {
  atfw::testing::mock_node node;
  node.set_id(node_id)
      .set_name(name)
      .set_type_id(static_cast<uint64_t>(atfw::component::logic_service_type::kComplianceSvr))
      .set_type_name("compliance-svr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", ready ? "1" : "0");
  return node;
}

static bool reload_discovery() {
  auto* module = logic_server_last_common_module();
  CASE_EXPECT_TRUE(module != nullptr);
  if (module == nullptr) {
    return false;
  }
  int result = module->reload();
  CASE_EXPECT_EQ(0, result);
  return result == 0;
}
}  // namespace

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance_sdk, filter_text_selects_ready_node_and_preserves_messages) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto ready = test.discovery().add_node(make_compliance_node(0x210001, "compliance-ready", true));
  auto unready = test.discovery().add_node(make_compliance_node(0x210002, "compliance-unready", false));
  atfw::testing::mock_node other_type;
  other_type.set_id(0x120001)
      .set_name("lobby-ready")
      .set_type_id(static_cast<uint64_t>(atfw::component::logic_service_type::kLobbySvr))
      .set_type_name("lobbysvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  auto other = test.discovery().add_node(other_type);
  CASE_EXPECT_TRUE(!!ready);
  CASE_EXPECT_TRUE(!!unready);
  CASE_EXPECT_TRUE(!!other);
  if (!ready || !unready || !other || !reload_discovery()) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }

  auto rpc_name = rpc::compliance::packer::get_full_name_of_filter_text();
  auto rule = test.ss().mock(
      rpc_name, atframework::compliance::SSFilterTextReq::descriptor()->full_name(),
      atframework::compliance::SSFilterTextRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request, google::protobuf::Message& response) -> rpc::result_code_type {
        CASE_EXPECT_EQ(static_cast<uint64_t>(0x210001), request.target_node_id);
        const auto& body = static_cast<const atframework::compliance::SSFilterTextReq&>(request.body);
        CASE_EXPECT_EQ(2, body.items_size());
        if (body.items_size() != 2) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
        }
        CASE_EXPECT_EQ("clean forbidden", body.items(0).text());
        CASE_EXPECT_TRUE(body.items(0).settings().return_original_text());
        CASE_EXPECT_EQ("[filtered]", body.items(0).settings().replacement_text());
        CASE_EXPECT_EQ("second", body.items(1).text());
        CASE_EXPECT_FALSE(body.items(1).settings().return_original_text());
        CASE_EXPECT_EQ("#", body.items(1).settings().replacement_text());
        auto& output = static_cast<atframework::compliance::SSFilterTextRsp&>(response);
        output.add_results()->set_original_text("clean forbidden");
        output.mutable_results(0)->set_filtered_text("clean [filtered]");
        output.mutable_results(0)->set_status(atframework::compliance::EN_TEXT_FILTER_STATUS_MATCHED);
        output.add_results()->set_filtered_text("second");
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  test.ss().expect(rpc_name).times(1).to_node(0x210001);

  auto task =
      test.run_task("filter_text_auto_route", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<atframework::compliance::SSFilterTextReq>(ctx);
        auto response = rpc::make_shared_message<atframework::compliance::SSFilterTextRsp>(ctx);
        request->add_items()->set_text("clean forbidden");
        request->mutable_items(0)->mutable_settings()->set_return_original_text(true);
        request->mutable_items(0)->mutable_settings()->set_replacement_text("[filtered]");
        request->add_items()->set_text("second");
        request->mutable_items(1)->mutable_settings()->set_replacement_text("#");
        int result = RPC_AWAIT_CODE_RESULT(rpc::compliance::filter_text(ctx, *request, *response));
        CASE_EXPECT_EQ(0, result);
        CASE_EXPECT_EQ(2, response->results_size());
        if (response->results_size() != 2) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
        }
        CASE_EXPECT_EQ("clean forbidden", response->results(0).original_text());
        CASE_EXPECT_EQ("clean [filtered]", response->results(0).filtered_text());
        CASE_EXPECT_EQ(atframework::compliance::EN_TEXT_FILTER_STATUS_MATCHED, response->results(0).status());
        CASE_EXPECT_EQ("second", response->results(1).filtered_text());
        CASE_EXPECT_TRUE(response->results(1).original_text().empty());
        CASE_EXPECT_EQ(atframework::compliance::EN_TEXT_FILTER_STATUS_OK, response->results(1).status());
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance_sdk, unavailable_nodes_do_not_send_requests) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  for (int stage = 0; stage < 2; ++stage) {
    if (stage == 1) {
      auto node = test.discovery().add_node(make_compliance_node(0x210003, "compliance-unready-only", false));
      CASE_EXPECT_TRUE(!!node);
      if (!node || !reload_discovery()) {
        CASE_EXPECT_EQ(0, test.stop());
        return;
      }
    }
    auto task = test.run_task(
        "filter_text_unavailable", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
          auto request = rpc::make_shared_message<atframework::compliance::SSFilterTextReq>(ctx);
          auto response = rpc::make_shared_message<atframework::compliance::SSFilterTextRsp>(ctx);
          request->add_items()->set_text("text to check");
          request->mutable_items(0)->mutable_settings()->set_return_original_text(false);
          request->mutable_items(0)->mutable_settings()->set_replacement_text("*");
          response->add_results()->set_filtered_text("previous output");
          int result = RPC_AWAIT_CODE_RESULT(rpc::compliance::filter_text(ctx, *request, *response));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_ATBUS_ERR_ATNODE_NOT_FOUND, result);
          CASE_EXPECT_EQ(1, response->results_size());
          if (response->results_size() == 1) {
            CASE_EXPECT_EQ("previous output", response->results(0).filtered_text());
          }
          RPC_RETURN_CODE(0);
        });
    CASE_EXPECT_FALSE(task.empty());
    if (!task.empty()) {
      auto result = test.wait(task, std::chrono::seconds{6});
      CASE_EXPECT_TRUE(result.task_exited);
      CASE_EXPECT_EQ(0, result.result_code);
    }
    CASE_EXPECT_EQ(static_cast<size_t>(0), test.transport().outbound_count());
  }
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance_sdk, empty_text_reaches_remote_service_and_preserves_error) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto remote = test.discovery().add_node(make_compliance_node(0x210004, "compliance-error", true));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote || !reload_discovery()) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto rpc_name = rpc::compliance::packer::get_full_name_of_filter_text();
  auto rule = test.ss().mock_error(rpc_name, PROJECT_NAMESPACE_ID::err::EN_SYS_NOT_SUPPORT);
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  test.ss().expect(rpc_name).times(1).to_node(0x210004);

  auto task =
      test.run_task("filter_empty_text", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<atframework::compliance::SSFilterTextReq>(ctx);
        auto response = rpc::make_shared_message<atframework::compliance::SSFilterTextRsp>(ctx);
        request->add_items()->mutable_settings()->set_return_original_text(false);
        request->mutable_items(0)->mutable_settings()->set_replacement_text("*");
        response->add_results()->set_filtered_text("stale output");
        int result = RPC_AWAIT_CODE_RESULT(rpc::compliance::filter_text(ctx, *request, *response));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOT_SUPPORT, result);
        CASE_EXPECT_EQ(1, response->results_size());
        if (response->results_size() == 1) {
          CASE_EXPECT_EQ("stale output", response->results(0).filtered_text());
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance_sdk, empty_batch_does_not_route_and_clears_output) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  auto task =
      test.run_task("filter_empty_batch", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<atframework::compliance::SSFilterTextReq>(ctx);
        auto response = rpc::make_shared_message<atframework::compliance::SSFilterTextRsp>(ctx);
        response->add_results()->set_filtered_text("stale output");
        int result = RPC_AWAIT_CODE_RESULT(rpc::compliance::filter_text(ctx, *request, *response));
        CASE_EXPECT_EQ(0, result);
        CASE_EXPECT_EQ(0, response->results_size());
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(static_cast<size_t>(0), test.transport().outbound_count());
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance_sdk, remote_response_is_forwarded_without_count_validation) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  auto remote = test.discovery().add_node(make_compliance_node(0x210005, "compliance-incomplete", true));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote || !reload_discovery()) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto rpc_name = rpc::compliance::packer::get_full_name_of_filter_text();
  auto rule = test.ss().mock(
      rpc_name, atframework::compliance::SSFilterTextReq::descriptor()->full_name(),
      atframework::compliance::SSFilterTextRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        static_cast<atframework::compliance::SSFilterTextRsp&>(response).add_results()->set_filtered_text("partial");
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);
  test.ss().expect(rpc_name).times(1);
  auto task =
      test.run_task("filter_incomplete_batch", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
        auto request = rpc::make_shared_message<atframework::compliance::SSFilterTextReq>(ctx);
        auto response = rpc::make_shared_message<atframework::compliance::SSFilterTextRsp>(ctx);
        request->add_items()->set_text("first");
        request->add_items()->set_text("second");
        int result = RPC_AWAIT_CODE_RESULT(rpc::compliance::filter_text(ctx, *request, *response));
        CASE_EXPECT_EQ(0, result);
        CASE_EXPECT_EQ(1, response->results_size());
        if (response->results_size() == 1) {
          CASE_EXPECT_EQ("partial", response->results(0).filtered_text());
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{6});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}
