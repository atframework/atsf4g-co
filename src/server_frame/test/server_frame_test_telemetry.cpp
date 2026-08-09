// Copyright 2026 atframework

#include <chrono>
#include <string>

#include <common/file_system.h>

#include <atframework/testing/runtime.h>

#include "frame/test_macros.h"
#include "rpc/rpc_context.h"

namespace {
int32_t run_tracer_task(atfw::testing::runtime &test, gsl::string_view name) {
  auto task = test.run_task(name, std::chrono::seconds{2}, [name](rpc::context &ctx) -> rpc::result_code_type {
    // A tracer can always be started in a fixture: with no exporter configured it is a noop span, with
    // file-only exporters it is exported through the real provider chain (see the telemetry section of
    // doc/docs/development/rpc-unit-test.md).
    rpc::telemetry::trace_start_option trace_option;
    rpc::telemetry::tracer tracer = ctx.make_tracer(name, std::move(trace_option));
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    return -1;
  }
  auto result = test.wait(task, std::chrono::seconds{5});
  if (!result.task_exited) {
    return -2;
  }
  return result.result_code;
}
}  // namespace

// server_frame component: file-only exporters (otlp_file) go through the real telemetry provider chain and
// write into the case working directory inside the build tree, with no outbound connection (8.9). This case
// runs before telemetry_default_noop_no_network because the opentelemetry provider chain is process-lifetime:
// the first fixture's exporter configuration is the one that takes effect.
CASE_TEST(server_frame_unit_test, telemetry_file_only_export) {
  const std::string trace_file = "telemetry-export-test.trace.log";
  atfw::util::file_system::remove(trace_file.c_str());

  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.telemetry_otlp_file_pattern = "telemetry-export-test";
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  CASE_EXPECT_EQ(0, static_cast<int>(run_tracer_task(test, "rpc-unit-test/file-only")));
  CASE_EXPECT_EQ(0, test.stop());

  CASE_EXPECT_TRUE(atfw::util::file_system::is_exist(trace_file.c_str()));
  std::string content;
  if (atfw::util::file_system::get_file_content(content, trace_file.c_str(), true)) {
    CASE_EXPECT_TRUE(!content.empty());
    CASE_EXPECT_TRUE(content.find("rpc-unit-test/file-only") != std::string::npos);
  } else {
    CASE_EXPECT_TRUE(false);
  }
  atfw::util::file_system::remove(trace_file.c_str());
}

// server_frame component: the default fixture writes no logic.telemetry section, so trace/metrics/logs fall
// back to noop providers and never touch the network; starting a tracer neither fails nor crashes (8.9).
CASE_TEST(server_frame_unit_test, telemetry_default_noop_no_network) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  CASE_EXPECT_EQ(0, static_cast<int>(run_tracer_task(test, "rpc-unit-test/noop")));
  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: outbound (otlp_grpc/otlp_http/prometheus_push) and inbound (prometheus_pull)
// exporters are forbidden in unit tests; start() must fail fast with a diagnostic (8.9).
CASE_TEST(server_frame_unit_test, telemetry_forbidden_exporters_fail_fast) {
  atfw::testing::runtime test;

  atfw::testing::runtime_options options;
  options.telemetry_otlp_grpc_endpoint = "127.0.0.1:4317";
  CASE_EXPECT_TRUE(0 != test.start(options));
  CASE_EXPECT_TRUE(test.get_diagnostic().find("telemetry_otlp_grpc_endpoint") != std::string::npos);
  CASE_EXPECT_TRUE(!test.is_running());

  options = atfw::testing::runtime_options{};
  options.telemetry_otlp_http_endpoint = "http://127.0.0.1:4318";
  CASE_EXPECT_TRUE(0 != test.start(options));
  CASE_EXPECT_TRUE(test.get_diagnostic().find("telemetry_otlp_http_endpoint") != std::string::npos);

  options = atfw::testing::runtime_options{};
  options.telemetry_prometheus_push_host = "127.0.0.1";
  CASE_EXPECT_TRUE(0 != test.start(options));
  CASE_EXPECT_TRUE(test.get_diagnostic().find("telemetry_prometheus_push_host") != std::string::npos);

  options = atfw::testing::runtime_options{};
  options.telemetry_prometheus_pull_url = "http://127.0.0.1:9090/metrics";
  CASE_EXPECT_TRUE(0 != test.start(options));
  CASE_EXPECT_TRUE(test.get_diagnostic().find("telemetry_prometheus_pull_url") != std::string::npos);
}
