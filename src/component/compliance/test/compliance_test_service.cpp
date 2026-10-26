// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/compliance_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>

#include <config/extern_service_types.h>
#include <frame/test_macros.h>

#include <chrono>
#include <cstdint>
#include <string>

#include "app/handle_ss_rpc_complianceservice.atfw.gen.h"
#include "rpc/compliance/complianceservice.atfw.gen.h"

// CASE_TEST registers the case through a framework-owned static object.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance, unimplemented_filter_returns_error_without_results) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  options.setup_callback = [](atfw::testing::runtime&) {
    return handle::compliance::register_handles_for_complianceservice();
  };
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  constexpr uint64_t kSourceNodeId = 0x120001;
  constexpr uint64_t kSequence = 101;
  atfw::testing::mock_node source_node;
  source_node.set_id(kSourceNodeId).set_name("compliance-test-source").set_type_id(12).set_type_name("lobbysvr");
  auto sender = test.discovery().add_node(source_node);
  CASE_EXPECT_TRUE(!!sender);
  if (!sender) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }

  atframework::compliance::SSFilterTextReq request;
  request.set_text("text to check");
  request.mutable_settings()->set_return_original_text(true);
  request.mutable_settings()->set_replacement_text("[filtered]");

  auto rpc_name = rpc::compliance::packer::get_full_name_of_filter_text();
  atframework::SSMsg message;
  auto* head = message.mutable_head();
  head->set_node_id(kSourceNodeId);
  head->set_node_name("compliance-test-source");
  head->set_source_task_id(88);
  head->set_sequence(kSequence);
  head->mutable_rpc_request()->set_rpc_name(rpc_name.data(), rpc_name.size());
  head->mutable_rpc_request()->set_type_url(atframework::compliance::SSFilterTextReq::descriptor()->full_name());
  CASE_EXPECT_TRUE(request.SerializeToString(message.mutable_body_bin()));
  std::string payload;
  CASE_EXPECT_TRUE(message.SerializeToString(&payload));
  CASE_EXPECT_EQ(
      0, test.transport().inject_inbound(
             sender, static_cast<int32_t>(atfw::component::message_type::kInServerMessage),
             gsl::span<const unsigned char>{reinterpret_cast<const unsigned char*>(payload.data()), payload.size()},
             kSequence));

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (test.transport().outbound_count_to(kSourceNodeId) == 0 && std::chrono::steady_clock::now() < deadline) {
    test.pump_once();
  }

  size_t response_count = 0;
  for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
    const auto* record = test.transport().outbound_at(i);
    if (record == nullptr || record->target_node_id != kSourceNodeId) {
      continue;
    }
    ++response_count;
    atframework::SSMsg response;
    CASE_EXPECT_TRUE(response.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size())));
    CASE_EXPECT_EQ(kSequence, response.head().sequence());
    CASE_EXPECT_TRUE(response.head().has_rpc_response());
    CASE_EXPECT_EQ(std::string(rpc_name.data(), rpc_name.size()), response.head().rpc_response().rpc_name());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOT_SUPPORT, response.head().error_code());
    atframework::compliance::SSFilterTextRsp body;
    CASE_EXPECT_TRUE(body.ParseFromString(response.body_bin()));
    CASE_EXPECT_TRUE(body.original_text().empty());
    CASE_EXPECT_TRUE(body.filtered_text().empty());
  }
  CASE_EXPECT_EQ(static_cast<size_t>(1), response_count);
  CASE_EXPECT_EQ(0, test.stop());
}
