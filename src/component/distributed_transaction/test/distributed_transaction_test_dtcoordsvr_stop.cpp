// Copyright 2026 atframework
//
// transaction_manager::stop() rejects new work but keeps existing handles valid until cleanup().
// This dedicated executable prevents the shutdown state from leaking into other coordinator cases.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/dtcoordsvr_config.pb.h>
#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "dt_test_common.h"  // NOLINT(build/include_subdir)
#include "logic/transaction_manager.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/rpc_shared_message.h"
#include "rpc/rpc_utils.h"

namespace {
void setup_dtcoordsvr_config_loader() {
  logic_config::me()->set_server_instance_config_loader([](atfw::atapp::app& app_, logic_config&,
                                                           logic_config::server_instance_config_ptr& to) {
    auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::distributed_system::config::dtcoordsvr_cfg>();
    // 注解字段（含 lru_max_cache_count 默认 200000）由 parse_configures_into 按注解默认值生效，
    // 本用例不依赖容量淘汰，无需注入容量
    app_.parse_configures_into(*config_ptr, "dtcoordsvr", "ATAPP_DTCOORDSVR");
    to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
  });
}
}  // namespace

CASE_TEST(component_dtcoordsvr_stop, stop_drains_existing_transactions_before_cleanup) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::db};
  options.setup_callback = [](atfw::testing::runtime&) -> int {
    setup_dtcoordsvr_config_loader();
    return 0;
  };
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_distribute_transaction>();

  auto prepare =
      test.run_task("dtcoordsvr_before_stop", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
        atfw::distributed_system::transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "dtcoordsvr-stop-uuid-1", {"pa"});
        int32_t res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage)));
        CASE_EXPECT_EQ(0, res);

        // Before stop the record is readable.
        transaction_manager::transaction_ptr_type trans;
        atfw::distributed_system::transaction_metadata metadata;
        metadata.set_transaction_uuid("dtcoordsvr-stop-uuid-1");
        res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!trans);
        atfw::distributed_system::transaction_blob_storage memory_storage;
        dt_test::make_prepared_storage(memory_storage, "dtcoordsvr-stop-memory", {"pa"}, true);
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(memory_storage))));
        CASE_EXPECT_EQ(2, transaction_manager::me()->get_lru_size_for_unit_test());
        RPC_RETURN_CODE(0);
      });
  auto prepared = test.wait(prepare, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(prepared.task_exited);
  CASE_EXPECT_EQ(0, prepared.result_code);

  bool ttl_entered = false;
  bool release_io = false;
  auto ttl_rule = rpc::db::distribute_transaction::mock::set_ttl(
      [&ttl_entered, &release_io](rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_distribute_transaction& input,
                                  uint64_t ttl, rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        CASE_EXPECT_EQ(std::string("dtcoordsvr-inflight-create"), input.transaction_uuid());
        CASE_EXPECT_GT(ttl, 0);
        ttl_entered = true;
        while (!release_io) {
          auto result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
          if (result < 0) {
            RPC_RETURN_CODE(result);
          }
        }
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ttl_rule);
  auto creating = test.run_task(
      "dtcoordsvr_inflight_create", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
        atfw::distributed_system::transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "dtcoordsvr-inflight-create", {"pa"});
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&ttl_entered]() { return ttl_entered; }));

  bool fetch_entered = false;
  auto fetch_rule = rpc::db::distribute_transaction::mock::get_all(
      [&fetch_entered, &release_io](rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_distribute_transaction& input,
                                    PROJECT_NAMESPACE_ID::table_distribute_transaction& output,
                                    rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
        fetch_entered = true;
        while (!release_io) {
          auto result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
          if (result < 0) {
            RPC_RETURN_CODE(result);
          }
        }
        atfw::distributed_system::transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, input.transaction_uuid(), {"pa"});
        output.set_zone_id(input.zone_id());
        output.set_transaction_uuid(input.transaction_uuid());
        CASE_EXPECT_TRUE(output.mutable_blob_data()->PackFrom(storage));
        meta.version = 1;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!fetch_rule);
  auto fetching = test.run_task(
      "dtcoordsvr_inflight_fetch", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
        atfw::distributed_system::transaction_metadata metadata;
        metadata.set_transaction_uuid("dtcoordsvr-inflight-fetch");
        transaction_manager::transaction_ptr_type output;
        auto result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, output));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN, result);
        CASE_EXPECT_FALSE(!!output);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&fetch_entered]() { return fetch_entered; }));

  std::vector<atfw::util::memory::weak_rc_ptr<transaction_manager::transaction_lru_map_type::value_cache_type>>
      watchers(2);
  auto task = test.run_task(
      "dtcoordsvr_stop", std::chrono::seconds{4}, [&test, &watchers](rpc::context& ctx) -> rpc::result_code_type {
        std::vector<transaction_manager::transaction_ptr_type> transactions(2);
        for (size_t i = 0; i < transactions.size(); ++i) {
          atfw::distributed_system::transaction_metadata metadata;
          metadata.set_transaction_uuid(i == 0 ? "dtcoordsvr-stop-uuid-1" : "dtcoordsvr-stop-memory");
          metadata.set_memory_only(i != 0);
          CASE_EXPECT_EQ(
              0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, transactions[i])));
          if (!CASE_EXPECT_TRUE(!!transactions[i])) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          watchers[i] = transactions[i];
        }

        // stop() is idempotent and leaves both ready records and the pending fetch cached for draining.
        transaction_manager::me()->stop();
        transaction_manager::me()->stop();
        CASE_EXPECT_EQ(3, transaction_manager::me()->get_lru_size_for_unit_test());

        const auto db_calls_before = test.db().calls("distribute_transaction");
        for (size_t i = 0; i < transactions.size(); ++i) {
          auto& trans = transactions[i];
          CASE_EXPECT_FALSE(trans->removed);
          // Rejected reads must clear an existing output handle, for persistent and memory-only records.
          transaction_manager::transaction_ptr_type poisoned = trans;
          auto res = RPC_AWAIT_CODE_RESULT(
              transaction_manager::me()->mutable_transaction(ctx, trans->data_object.metadata(), poisoned));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN, res);
          CASE_EXPECT_FALSE(!!poisoned);
          trans.reset();
          CASE_EXPECT_FALSE(watchers[i].expired());
        }

        for (bool memory_only : {false, true}) {
          atfw::distributed_system::transaction_blob_storage late_storage;
          dt_test::make_prepared_storage(late_storage, "dtcoordsvr-after-stop", {"pa"}, memory_only);
          CASE_EXPECT_EQ(
              PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN,
              RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(late_storage))));
          CASE_EXPECT_EQ(3, transaction_manager::me()->get_lru_size_for_unit_test());
        }
        CASE_EXPECT_EQ(db_calls_before, test.db().calls("distribute_transaction"));

        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  release_io = true;
  auto create_result = test.wait(creating, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(create_result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN, create_result.result_code);
  auto fetch_result = test.wait(fetching, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(fetch_result.task_exited);
  CASE_EXPECT_EQ(0, fetch_result.result_code);
  CASE_EXPECT_EQ(3, transaction_manager::me()->get_lru_size_for_unit_test());
  ttl_rule.reset();
  fetch_rule.reset();

  // Existing handles may still persist their decision while shutdown drains active tasks.
  auto draining = test.run_task(
      "dtcoordsvr_drain", std::chrono::seconds{4}, [&test, &watchers](rpc::context& ctx) -> rpc::result_code_type {
        for (auto& watcher : watchers) {
          auto trans = watcher.lock();
          if (!CASE_EXPECT_TRUE(!!trans)) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          CASE_EXPECT_FALSE(trans->removed);
          CASE_EXPECT_EQ(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                         trans->data_object.metadata().status());
          const auto db_calls_before = test.db().calls("distribute_transaction");
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
          CASE_EXPECT_EQ(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                         trans->data_object.metadata().status());
          if (trans->data_object.metadata().memory_only()) {
            CASE_EXPECT_EQ(db_calls_before, test.db().calls("distribute_transaction"));
          } else {
            rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> record{ctx};
            uint64_t version = 0;
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::get_all(
                                  ctx, 0, trans->data_object.metadata().transaction_uuid(), *record, version)));
            atfw::distributed_system::transaction_blob_storage persisted;
            CASE_EXPECT_TRUE(record->blob_data().UnpackTo(&persisted));
            CASE_EXPECT_EQ(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                           persisted.metadata().status());
            CASE_EXPECT_EQ(2, version);
          }
        }
        RPC_RETURN_CODE(0);
      });
  auto drained = test.wait(draining, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(drained.task_exited);
  CASE_EXPECT_EQ(0, drained.result_code);

  // cleanup() invalidates retained handles and releases records owned only by the cache.
  auto retained = watchers.front().lock();
  CASE_EXPECT_TRUE(!!retained);
  CASE_EXPECT_FALSE(watchers.back().expired());
  transaction_manager::me()->cleanup();
  transaction_manager::me()->cleanup();
  CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
  if (retained) {
    CASE_EXPECT_TRUE(retained->removed);
  }
  CASE_EXPECT_TRUE(watchers.back().expired());
  retained.reset();
  CASE_EXPECT_TRUE(watchers.front().expired());

  // Cleanup must not reopen admission or reset the process-lifetime shutdown state.
  auto after_cleanup = test.run_task(
      "dtcoordsvr_after_cleanup", std::chrono::seconds{4}, [&test](rpc::context& ctx) -> rpc::result_code_type {
        const auto db_calls_before = test.db().calls("distribute_transaction");
        for (bool memory_only : {false, true}) {
          atfw::distributed_system::transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, "dtcoordsvr-after-cleanup", {"pa"}, memory_only);
          transaction_manager::transaction_ptr_type output;
          CASE_EXPECT_EQ(
              PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN,
              RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, storage.metadata(), output)));
          CASE_EXPECT_FALSE(!!output);
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        }
        CASE_EXPECT_EQ(db_calls_before, test.db().calls("distribute_transaction"));
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        RPC_RETURN_CODE(0);
      });
  auto cleaned = test.wait(after_cleanup, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(cleaned.task_exited);
  CASE_EXPECT_EQ(0, cleaned.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}
