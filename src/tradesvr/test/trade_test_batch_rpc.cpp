// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/trade_order_service.pb.h>
#include <protocol/pbdesc/trade_search_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>
#include <config/extern_service_types.h>
#include <frame/test_macros.h>
#include <logic/logic_server_setup.h>
#include <rpc/trade/order.h>
#include <rpc/trade/search.h>
#include <rpc/trade_order/tradeorderservice.atfw.gen.h>
#include <rpc/trade_search/tradesearchservice.atfw.gen.h>

#include <chrono>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
using namespace atframework::trade;  // NOLINT(build/namespaces)
constexpr uint64_t kFirstServer = 0x350001;
constexpr uint64_t kSecondServer = 0x350002;

static bool add_node(atfw::testing::runtime& test, uint64_t id, const char* name, uint32_t type,
                     const char* type_name) {
  atfw::testing::mock_node node;
  node.set_id(id).set_name(name).set_type_id(type).set_type_name(type_name).add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  return !!remote;
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
CASE_TEST(trade_sdk, batch_lock_and_unlock_by_server) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  constexpr auto kOrderType = PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kTradesvrOrderFieldNumber;
  if (!add_node(test, kFirstServer, "order-1", kOrderType, "tradesvr-order") ||
      !add_node(test, kSecondServer, "order-2", kOrderType, "tradesvr-order")) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto lock_rule =
      rpc::trade_order::mock::fetch_try_lock([](rpc::context&, const SSTradeOrderFetchLockOrderReq& request,
                                                SSTradeOrderFetchLockOrderRsp& response) -> rpc::result_code_type {
        for (const auto& product : request.try_lock()) {
          *response.add_locked()->mutable_product_key() = product.product_key();
        }
        RPC_RETURN_CODE(0);
      });
  atfw::testing::ss_rule_options error_options;
  error_options.match_node_id = kSecondServer;
  auto error_rule = test.ss().mock_error(rpc::trade_order::packer::get_full_name_of_fetch_unlock(),
                                         atfw::shared::EN_ERR_NOT_IMPLEMENTED, error_options);
  auto unlock_rule =
      rpc::trade_order::mock::fetch_unlock([](rpc::context&, const SSTradeOrderFetchUnlockOrderReq& request,
                                              SSTradeOrderFetchUnlockOrderRsp& response) -> rpc::result_code_type {
        for (const auto& product : request.unlock()) {
          *response.add_unlocked()->mutable_trade_order_ticket_id() = product.trade_order_ticket_id();
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(lock_rule && error_rule && unlock_rule);
  auto task = test.run_task("batch locks", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
    SSTradeOrderFetchLockOrderReq first, second;
    first.add_try_lock()->mutable_product_key()->set_trade_product_id(101);
    second.add_try_lock()->mutable_product_key()->set_trade_product_id(202);
    std::unordered_map<uint64_t, SSTradeOrderFetchLockOrderReq*> requests{
        {kFirstServer, &first},
        {kSecondServer, &second},
    };
    SSTradeOrderFetchLockOrderRsp supplied;
    std::unordered_map<uint64_t, SSTradeOrderFetchLockOrderRsp*> responses{{kSecondServer, &supplied}};
    int result = RPC_AWAIT_CODE_RESULT(batch_try_fetch_lock(ctx, requests, responses));
    CASE_EXPECT_EQ(0, result);
    CASE_EXPECT_TRUE(responses[kSecondServer] == &supplied);
    for (auto server : {kFirstServer, kSecondServer}) {
      auto* response = responses[server];
      CASE_EXPECT_TRUE(response != nullptr);
      if (response != nullptr) {
        SSTradeOrderFetchLockOrderRsp expected;
        expected.add_locked()->mutable_product_key()->set_trade_product_id(server == kFirstServer ? 101 : 202);
        CASE_EXPECT_EQ(expected.SerializeAsString(), response->SerializeAsString());
      }
    }

    SSTradeOrderFetchUnlockOrderReq unlock;
    unlock.add_unlock()->add_trade_order_ticket_id(303);
    std::unordered_map<uint64_t, SSTradeOrderFetchUnlockOrderReq*> unlock_requests{
        {kFirstServer, &unlock},
        {kSecondServer, &unlock},
    };
    std::unordered_map<uint64_t, SSTradeOrderFetchUnlockOrderRsp*> unlocked{{kFirstServer, nullptr}};
    result = RPC_AWAIT_CODE_RESULT(batch_fetch_unlock(ctx, unlock_requests, unlocked));
    CASE_EXPECT_EQ(atfw::shared::EN_ERR_NOT_IMPLEMENTED, result);
    auto* success = unlocked[kFirstServer];
    CASE_EXPECT_TRUE(success != nullptr);
    if (success != nullptr) {
      SSTradeOrderFetchUnlockOrderRsp expected;
      expected.add_unlocked()->add_trade_order_ticket_id(303);
      CASE_EXPECT_EQ(expected.SerializeAsString(), success->SerializeAsString());
    }
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
CASE_TEST(trade_sdk, get_orders_across_areas) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  if (!add_node(test, kFirstServer, "search",
                PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kTradesvrSearchFieldNumber,
                "tradesvr-search")) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  logic_server_last_common_module()->reload();
  atfw::testing::ss_rule_options once;
  once.times = 1;
  auto error_rule = test.ss().mock_error(rpc::trade_search::packer::get_full_name_of_get_order(),
                                         atfw::shared::EN_ERR_NOT_IMPLEMENTED, once);
  auto rule = rpc::trade_search::mock::get_order([](rpc::context&, const SSTradeSearchGetOrderReq& request,
                                                    SSTradeSearchGetOrderRsp& response) -> rpc::result_code_type {
    for (const auto& key : request.ticket_key()) {
      if (key.trade_order_ticket_id() % 2 == 0) {
        *response.add_not_found_ticket_key() = key;
      } else {
        response.add_order_detail()->mutable_meta()->set_trade_order_ticket_id(key.trade_order_ticket_id());
      }
    }
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_TRUE(error_rule && rule);
  auto task = test.run_task("get orders", std::chrono::seconds{3}, [](rpc::context& ctx) -> rpc::result_code_type {
    std::vector<trade_order_ticket_key> keys;
    for (int area = 1; area <= 3; ++area) {
      for (int id = 1; id <= 2; ++id) {
        keys.emplace_back();
        keys.back().set_area_code(area);
        keys.back().set_order_type(DTradeOrderTicketMeta::kSalesOrder);
        keys.back().set_trade_order_ticket_id((10 * area) + id);
      }
    }
    SSTradeSearchGetOrderRsp response;
    int result = RPC_AWAIT_CODE_RESULT(get_orders(ctx, keys, response));
    CASE_EXPECT_EQ(atfw::shared::EN_ERR_NOT_IMPLEMENTED, result);
    CASE_EXPECT_EQ(2, response.order_detail_size());
    CASE_EXPECT_EQ(2, response.not_found_ticket_key_size());

    result = RPC_AWAIT_CODE_RESULT(get_orders(ctx, keys, response));
    CASE_EXPECT_EQ(0, result);
    std::set<int64_t> found, missing;
    for (const auto& order : response.order_detail()) {
      found.insert(order.meta().trade_order_ticket_id());
    }
    for (const auto& key : response.not_found_ticket_key()) {
      missing.insert(key.trade_order_ticket_id());
    }
    CASE_EXPECT_EQ(3, response.order_detail_size());
    CASE_EXPECT_EQ(3, response.not_found_ticket_key_size());
    CASE_EXPECT_TRUE((found == std::set<int64_t>{11, 21, 31}));
    CASE_EXPECT_TRUE((missing == std::set<int64_t>{12, 22, 32}));
    RPC_RETURN_CODE(0);
  });
  CASE_EXPECT_FALSE(task.empty());
  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}
}  // namespace
