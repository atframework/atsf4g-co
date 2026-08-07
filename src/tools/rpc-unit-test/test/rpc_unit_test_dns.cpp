// Copyright 2026 atframework

#include <atframework/testing/mock_dns.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <string>
#include <vector>

#include "frame/test_macros.h"
#include "rpc/dns/lookup.h"

CASE_TEST(rpc_unit_test, dns_lookup_single_a_record) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::dns};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  auto rule = test.dns().mock_a("unit-test.local", "10.1.2.3");
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.dns().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task = test.run_task("dns_lookup", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    std::vector<rpc::dns::address_record> records;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "unit-test.local", records));
    CASE_EXPECT_EQ(1, static_cast<int>(records.size()));
    if (!records.empty()) {
      CASE_EXPECT_EQ(static_cast<int>(rpc::dns::address_type::kA), static_cast<int>(records[0].type));
      CASE_EXPECT_EQ("10.1.2.3", records[0].address);
    }
    RPC_RETURN_CODE(res);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_FALSE(result.task_timed_out);
  CASE_EXPECT_FALSE(result.runtime_poisoned);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(1, static_cast<int>(test.dns().calls("unit-test.local")));
  const atframework::testing::dns_request_record *record = test.dns().call_at(0);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_TRUE(record->matched_rule);
    CASE_EXPECT_NE(0, static_cast<int64_t>(record->sequence));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, dns_lookup_multiple_records_and_error) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::dns};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  rpc::dns::details::callback_data_type multi;
  multi.push_back(rpc::dns::address_record{rpc::dns::address_type::kA, "10.0.0.1"});
  multi.push_back(rpc::dns::address_record{rpc::dns::address_type::kAAAA, "fd00::1"});
  auto rule_multi = test.dns().mock("multi.unit-test.local", multi);
  auto rule_err = test.dns().mock_error("broken.unit-test.local");
  CASE_EXPECT_TRUE(!!rule_multi);
  CASE_EXPECT_TRUE(!!rule_err);
  if (!rule_multi || !rule_err) {
    test.stop();
    return;
  }

  auto task =
      test.run_task("dns_multi_and_error", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        std::vector<rpc::dns::address_record> records;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "multi.unit-test.local", records));
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }
        CASE_EXPECT_EQ(2, static_cast<int>(records.size()));
        if (records.size() >= 2) {
          CASE_EXPECT_EQ(static_cast<int>(rpc::dns::address_type::kA), static_cast<int>(records[0].type));
          CASE_EXPECT_EQ(static_cast<int>(rpc::dns::address_type::kAAAA), static_cast<int>(records[1].type));
          CASE_EXPECT_EQ("fd00::1", records[1].address);
        }

        // Resolution failure surfaces as an empty record set.
        records.clear();
        res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "broken.unit-test.local", records));
        CASE_EXPECT_EQ(0, static_cast<int>(records.size()));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(1, static_cast<int>(test.dns().calls("multi.unit-test.local")));
  CASE_EXPECT_EQ(1, static_cast<int>(test.dns().calls("broken.unit-test.local")));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, dns_unmatched_lookup_fast_fail) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::dns};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // No rule registered: the lookup must complete fast with an empty record set instead of waiting
  // for the full lookup timeout.
  auto task = test.run_task("dns_unmatched", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    std::vector<rpc::dns::address_record> records;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "unregistered.unit-test.local", records));
    CASE_EXPECT_EQ(0, static_cast<int>(records.size()));
    RPC_RETURN_CODE(res);
  });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_FALSE(result.task_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(1, static_cast<int>(test.dns().unmatched_count()));
  CASE_EXPECT_EQ(1, static_cast<int>(test.dns().calls("unregistered.unit-test.local")));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, dns_no_response_times_out_task) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::dns};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  atframework::testing::dns_rule_options options_silent;
  options_silent.no_response = true;
  auto rule = test.dns().mock_a("silent.unit-test.local", "10.9.9.9", options_silent);
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    test.stop();
    return;
  }

  // The lookup never completes; the task-level timeout kills the task.
  auto task = test.run_task("dns_no_response", std::chrono::seconds{1}, [](rpc::context &ctx) -> rpc::result_code_type {
    std::vector<rpc::dns::address_record> records;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "silent.unit-test.local", records));
    RPC_RETURN_CODE(res);
  });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_timed_out || !result.task_exited || 0 != result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// delay_generations defers the response by full extra pump generations: a delay-2 lookup completes exactly
// two pumps later than a delay-0 lookup. The assertion uses the relative pump distance so it is immune to
// the absolute coroutine-resume latency of the task driver.
CASE_TEST(rpc_unit_test, dns_delay_generations_defer_response) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::dns};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  // Returns the number of pumps between the lookup being issued and the task completing.
  auto measure_completion_pumps = [&test](uint32_t delay_generations, const char *domain) -> int {
    atframework::testing::dns_rule_options rule_options;
    rule_options.delay_generations = delay_generations;
    auto rule = test.dns().mock_a(domain, "10.3.3.3", rule_options);
    CASE_EXPECT_TRUE(!!rule);

    bool done = false;
    auto task = test.run_task("dns_delay", std::chrono::seconds{5},
                              [&done, domain](rpc::context &ctx) -> rpc::result_code_type {
                                std::vector<rpc::dns::address_record> records;
                                int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, domain, records));
                                CASE_EXPECT_EQ(0, res);
                                CASE_EXPECT_EQ(1, static_cast<int>(records.size()));
                                if (!records.empty()) {
                                  CASE_EXPECT_EQ("10.3.3.3", records[0].address);
                                }
                                done = true;
                                RPC_RETURN_CODE(res);
                              });
    if (task.empty()) {
      CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
      return -1;
    }

    // Pump until the task actually issues the lookup (recorded by the engine at queue time).
    for (int i = 0; i < 16 && 0 == test.dns().calls(domain); ++i) {
      test.pump_once();
    }
    CASE_EXPECT_EQ(1, static_cast<int>(test.dns().calls(domain)));
    CASE_EXPECT_FALSE(done);

    int pumps = 0;
    while (!done && pumps < 16) {
      test.pump_once();
      ++pumps;
    }
    CASE_EXPECT_TRUE(done);

    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    return pumps;
  };

  int fast_pumps = measure_completion_pumps(0, "fast-d.unit-test.local");
  int slow_pumps = measure_completion_pumps(2, "slow.unit-test.local");
  CASE_EXPECT_TRUE(fast_pumps >= 0);
  CASE_EXPECT_EQ(2, slow_pumps - fast_pumps);

  CASE_EXPECT_EQ(0, test.stop());
}

// Rules with different delays complete out of order: the zero-delay lookup finishes first even though the
// delayed lookup was issued first.
CASE_TEST(rpc_unit_test, dns_out_of_order_responses) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::dns};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  atframework::testing::dns_rule_options slow_options;
  slow_options.delay_generations = 3;
  auto slow_rule = test.dns().mock_a("slow-ooo.unit-test.local", "10.4.0.1", slow_options);
  auto fast_rule = test.dns().mock_a("fast-ooo.unit-test.local", "10.4.0.2");
  CASE_EXPECT_TRUE(!!slow_rule);
  CASE_EXPECT_TRUE(!!fast_rule);
  if (!slow_rule || !fast_rule) {
    test.stop();
    return;
  }

  std::vector<std::string> completion_order;
  auto slow_task = test.run_task(
      "dns_slow", std::chrono::seconds{5}, [&completion_order](rpc::context &ctx) -> rpc::result_code_type {
        std::vector<rpc::dns::address_record> records;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "slow-ooo.unit-test.local", records));
        CASE_EXPECT_EQ(0, res);
        completion_order.push_back("slow");
        RPC_RETURN_CODE(res);
      });
  auto fast_task = test.run_task(
      "dns_fast", std::chrono::seconds{5}, [&completion_order](rpc::context &ctx) -> rpc::result_code_type {
        std::vector<rpc::dns::address_record> records;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, "fast-ooo.unit-test.local", records));
        CASE_EXPECT_EQ(0, res);
        completion_order.push_back("fast");
        RPC_RETURN_CODE(res);
      });
  if (slow_task.empty() || fast_task.empty()) {
    test.stop();
    return;
  }

  // Waiting on the slow task drives the pump until it exits; the fast task must have finished earlier.
  auto slow_result = test.wait(slow_task, std::chrono::seconds{5});
  auto fast_result = test.wait(fast_task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(slow_result.task_exited);
  CASE_EXPECT_TRUE(fast_result.task_exited);
  CASE_EXPECT_EQ(0, slow_result.result_code);
  CASE_EXPECT_EQ(0, fast_result.result_code);

  CASE_EXPECT_EQ(2, static_cast<int>(completion_order.size()));
  if (completion_order.size() >= 2) {
    CASE_EXPECT_EQ("fast", completion_order[0]);
    CASE_EXPECT_EQ("slow", completion_order[1]);
  }

  CASE_EXPECT_EQ(0, test.stop());
}
