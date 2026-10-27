// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/compliance_svr_config.pb.h>
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
#include <config/logic_config.h>
#include <frame/test_macros.h>
#include <logic/logic_server_setup.h>
#include <memory/object_allocator.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

#include "app/handle_ss_rpc_complianceservice.atfw.gen.h"
#include "logic/text_filter_manager.h"
#include "rpc/compliance/complianceservice.atfw.gen.h"

namespace {
std::string& get_configured_dictionary_path() {
  static std::string path;
  return path;
}

void configure_dictionary(const std::string& path) {
  get_configured_dictionary_path() = path;
  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app&, logic_config&, logic_config::server_instance_config_ptr& output) {
        auto configuration =
            atfw::component::memory::stl::make_strong_rc<atframework::compliance::config::compliance_svr_cfg>();
        configuration->set_text_filter_dictionary_file(get_configured_dictionary_path());
        output = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(configuration);
      });
}

bool start_filter_runtime(atfw::testing::runtime& test) {
  configure_dictionary(COMPLIANCE_TEST_DICTIONARY_FILE);
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  options.setup_callback = [](atfw::testing::runtime&) {
    return handle::compliance::register_handles_for_complianceservice();
  };
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return false;
  }
  int loaded = compliance::text_filter_manager::me()->init();
  CASE_EXPECT_EQ(0, loaded);
  return loaded == 0;
}

bool dispatch_filter(atfw::testing::runtime& test, const atframework::compliance::SSFilterTextReq& request,
                     atframework::compliance::SSFilterTextRsp& response, int expected_code) {
  constexpr uint64_t kSourceNodeId = 0x120001;
  auto sender = test.discovery().get_node_by_id(kSourceNodeId);
  if (!sender) {
    atfw::testing::mock_node source;
    source.set_id(kSourceNodeId).set_name("compliance-test-source").set_type_id(12).set_type_name("lobbysvr");
    sender = test.discovery().add_node(source);
  }
  CASE_EXPECT_TRUE(!!sender);
  if (!sender) {
    return false;
  }
  test.transport().clear_history();
  auto rpc_name = rpc::compliance::packer::get_full_name_of_filter_text();
  atframework::SSMsg message;
  auto* head = message.mutable_head();
  head->set_node_id(kSourceNodeId);
  head->set_node_name("compliance-test-source");
  head->set_source_task_id(88);
  head->set_sequence(101);
  head->mutable_rpc_request()->set_rpc_name(rpc_name.data(), rpc_name.size());
  head->mutable_rpc_request()->set_type_url(atframework::compliance::SSFilterTextReq::descriptor()->full_name());
  CASE_EXPECT_TRUE(request.SerializeToString(message.mutable_body_bin()));
  std::string payload;
  CASE_EXPECT_TRUE(message.SerializeToString(&payload));
  CASE_EXPECT_EQ(
      0,
      test.transport().inject_inbound(
          sender, static_cast<int32_t>(atfw::component::message_type::kInServerMessage),
          gsl::span<const unsigned char>{reinterpret_cast<const unsigned char*>(payload.data()), payload.size()}, 101));
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (test.transport().outbound_count_to(kSourceNodeId) == 0 && std::chrono::steady_clock::now() < deadline) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(static_cast<size_t>(1), test.transport().outbound_count_to(kSourceNodeId));
  for (size_t index = 0; index < test.transport().outbound_count(); ++index) {
    const auto* record = test.transport().outbound_at(index);
    if (record == nullptr || record->target_node_id != kSourceNodeId) {
      continue;
    }
    atframework::SSMsg envelope;
    CASE_EXPECT_TRUE(envelope.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size())));
    CASE_EXPECT_EQ(static_cast<uint64_t>(101), envelope.head().sequence());
    CASE_EXPECT_EQ(std::string(rpc_name.data(), rpc_name.size()), envelope.head().rpc_response().rpc_name());
    CASE_EXPECT_EQ(expected_code, envelope.head().error_code());
    return response.ParseFromString(envelope.body_bin());
  }
  return false;
}

void add_item(atframework::compliance::SSFilterTextReq& request, const std::string& text,
              const std::string& replacement, bool original = false) {
  auto* item = request.add_items();
  item->set_text(text);
  item->mutable_settings()->set_replacement_text(replacement);
  item->mutable_settings()->set_return_original_text(original);
}
}  // namespace

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance, batch_filter_preserves_order_and_replaces_overlapping_matches) {
  atfw::testing::runtime test;
  if (!start_filter_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  atframework::compliance::SSFilterTextReq request;
  add_item(request, "all clear", "*");
  add_item(request, "BAD.word EV\tI\nL 坏　，词!", "<x>", true);
  add_item(request, "abab badbad", "*");
  add_item(request, "normalCase", "*", true);
  add_item(request, "x bad y", "", true);
  add_item(request, "", "*", true);
  add_item(request, "敏\xE2\x80\x8B感，词", "#");
  atframework::compliance::SSFilterTextRsp response;
  CASE_EXPECT_TRUE(dispatch_filter(test, request, response, 0));
  CASE_EXPECT_EQ(request.items_size(), response.results_size());
  if (response.results_size() == 7) {
    const char* expected[] = {"all clear", "<x> <x> <x>!", "* **", "normalCase", "x  y", "", "#"};
    for (int index = 0; index < response.results_size(); ++index) {
      const auto& result = response.results(index);
      CASE_EXPECT_EQ(expected[index], result.filtered_text());
      CASE_EXPECT_EQ(request.items(index).settings().return_original_text() ? request.items(index).text() : "",
                     result.original_text());
      CASE_EXPECT_EQ(index == 0 || index == 3 || index == 5 ? atframework::compliance::EN_TEXT_FILTER_STATUS_OK
                                                            : atframework::compliance::EN_TEXT_FILTER_STATUS_MATCHED,
                     result.status());
    }
  }
  request.Clear();
  CASE_EXPECT_TRUE(dispatch_filter(test, request, response, 0));
  CASE_EXPECT_EQ(0, response.results_size());
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance, invalid_wire_text_and_missing_dictionary_return_no_results) {
  atfw::testing::runtime test;
  if (!start_filter_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  atframework::compliance::SSFilterTextReq request;
  add_item(request, "bad", "*");
  add_item(request, std::string{"bad\xFF", 4}, "*");
  atframework::compliance::SSFilterTextRsp response;
  // Protobuf rejects invalid UTF-8 before the action is entered.
  CASE_EXPECT_TRUE(dispatch_filter(test, request, response, PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK));
  CASE_EXPECT_EQ(0, response.results_size());
  request.mutable_items()->RemoveLast();
  compliance::text_filter_manager::me()->cleanup();
  CASE_EXPECT_TRUE(dispatch_filter(test, request, response, PROJECT_NAMESPACE_ID::err::EN_SYS_INIT));
  CASE_EXPECT_EQ(0, response.results_size());
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization, misc-use-anonymous-namespace)
CASE_TEST(component_compliance, failed_reload_keeps_the_previous_dictionary) {
  atfw::testing::runtime test;
  if (!start_filter_runtime(test)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto snapshot = compliance::text_filter_manager::me()->get_dictionary();
  const std::string dictionary_path = COMPLIANCE_TEST_DICTIONARY_FILE;
  const std::string invalid_paths[] = {dictionary_path + ".missing", "invalid-text-filter-dictionary.acv2"};
  for (const auto& path : invalid_paths) {
    if (path == "invalid-text-filter-dictionary.acv2") {
      // NOLINTNEXTLINE(bugprone-signed-bitwise)
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      output << "invalid dictionary";
      CASE_EXPECT_TRUE(!!output);
    }
    configure_dictionary(path);
    CASE_EXPECT_EQ(0, logic_server_last_common_module()->reload());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT, compliance::text_filter_manager::me()->reload());
    CASE_EXPECT_TRUE(snapshot == compliance::text_filter_manager::me()->get_dictionary());
  }
  atframework::compliance::SSFilterTextReq request;
  add_item(request, "BAD", "*");
  atframework::compliance::SSFilterTextRsp response;
  CASE_EXPECT_TRUE(dispatch_filter(test, request, response, 0));
  CASE_EXPECT_EQ(1, response.results_size());
  if (response.results_size() == 1) {
    CASE_EXPECT_EQ("*", response.results(0).filtered_text());
  }
  CASE_EXPECT_EQ(0, test.stop());
}
