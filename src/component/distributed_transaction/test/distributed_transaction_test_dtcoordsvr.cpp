// Copyright 2026 atframework
//
// dtcoordsvr coordinator unit tests (UNIT_TEST_EXECUTION_PLAN.md section 4.4): transaction_manager
// public API (create/mutable/save/try_commit/try_reject/participant acks/try_remove/tick) plus the
// seven RPC actions. The manager is a process-lifetime singleton, so every case uses a unique UUID
// prefix and the LRU capacity assertions only rely on relative visit order, never on absolute
// leftover counts from earlier cases.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <protocol/config/dtcoordsvr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>

#include <std/explicit_declare.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "dt_test_common.h"
#include "app/handle_ss_rpc_dtcoordsvrservice.atfw.gen.h"
#include "logic/action/task_action_commit.h"
#include "logic/action/task_action_commit_participator.h"
#include "logic/action/task_action_create.h"
#include "logic/action/task_action_query.h"
#include "logic/action/task_action_reject.h"
#include "logic/action/task_action_reject_participator.h"
#include "logic/action/task_action_remove.h"
#include "logic/transaction_manager.h"
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/rpc_shared_message.h"
#include "utility/protobuf_mini_dumper.h"

namespace {
using atfw::distributed_system::EnDistibutedTransactionStatus;
using atfw::distributed_system::transaction_blob_storage;
using atfw::distributed_system::transaction_metadata;
using table_type = PROJECT_NAMESPACE_ID::table_distribute_transaction;
using op_type = rpc::db::hash_table::unit_test_request::op_type;

constexpr uint32_t kTestZoneId = 0;  // non-replication transactions live in zone 0

// Register the same server-instance config loader as dtcoordsvr_main.cpp. lru_max_cache_count has
// no CONFIGURE annotation so the explicit value (2, drives the tick eviction assertions) sticks;
// annotated fields (grace 5s, max TTL 30d, default timeout 10s) are re-applied by
// parse_configures_into and are the effective values asserted below.
void setup_dtcoordsvr_config_loader() {
  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app& app_, logic_config&, logic_config::server_instance_config_ptr& to) {
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::distributed_system::config::dtcoordsvr_cfg>();
        config_ptr->set_lru_max_cache_count(2);
        app_.parse_configures_into(*config_ptr, "dtcoordsvr", "ATAPP_DTCOORDSVR");
        to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });
}

// The rpc-unit-test runtime resets process-lifetime dispatcher registrations on every start, so the
// handles must be re-registered for each fixture.
void register_dtcoordsvr_handles() {
  handle::transaction::register_handles_for_dtcoordsvrservice();
}

rpc::result_code_type db_read_record(rpc::context& ctx, const std::string& uuid, table_type& output,
                                     uint64_t& version) {
  rpc::shared_message<table_type> storage{ctx};
  int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::get_all(ctx, kTestZoneId, uuid, *storage, version));
  if (res >= 0) {
    protobuf_copy_message(output, *storage);
  }
  RPC_RETURN_CODE(res);
}

rpc::result_code_type db_write_record(rpc::context& ctx, const std::string& uuid,
                                      const transaction_blob_storage& storage, uint64_t& version) {
  rpc::shared_message<table_type> db_data{ctx};
  db_data->set_zone_id(kTestZoneId);
  db_data->set_transaction_uuid(uuid);
  if (!db_data->mutable_blob_data()->PackFrom(storage)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }
  version = 0;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, db_data, version)));
}

atfw::testing::runtime_options make_dtcoordsvr_runtime_options() {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::db};
  options.setup_callback = [](atfw::testing::runtime&) -> int {
    setup_dtcoordsvr_config_loader();
    register_dtcoordsvr_handles();
    return 0;
  };
  return options;
}
}  // namespace

// ============ create / mutable / TTL / eviction / DT-007 / DT-009 / DT-019 ============

CASE_TEST(component_dtcoordsvr, manager_create_query_ttl_and_eviction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  test.db().register_message_type<table_type>();

  std::vector<uint64_t> ttl_values;
  auto ttl_rule = rpc::db::distribute_transaction::mock::set_ttl(
      [&ttl_values](rpc::context&, const table_type&, uint64_t ttl_second,
                    rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        ttl_values.push_back(ttl_second);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ttl_rule);

  auto task = test.run_task("manager_lifecycle", std::chrono::seconds{10}, [&test](rpc::context& ctx) -> rpc::result_code_type {
    // --- parameter guards: empty uuid / no participators never touch the DB or LRU
    size_t db_ops_before = test.db().calls("distribute_transaction");

    transaction_blob_storage invalid_storage;
    dt_test::make_prepared_storage(invalid_storage, "", {"pa"});
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(invalid_storage))));

    transaction_blob_storage no_participator_storage;
    dt_test::make_prepared_storage(no_participator_storage, "mgr-empty-participators", {});
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(
                       ctx, std::move(no_participator_storage))));
    CASE_EXPECT_EQ(db_ops_before, test.db().calls("distribute_transaction"));

    // --- create writes the DB once with an unconditional CAS (expected_version=0) and fills the LRU
    transaction_blob_storage storage_a;
    dt_test::make_prepared_storage(storage_a, "mgr-a", {"pa"}, false, std::chrono::seconds{30});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_a))));

    table_type record;
    uint64_t version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-a", record, version)));
    CASE_EXPECT_EQ(1, version);
    CASE_EXPECT_EQ("mgr-a", record.transaction_uuid());
    transaction_blob_storage reloaded;
    CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&reloaded));
    CASE_EXPECT_EQ(1, reloaded.participators().size());
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                   reloaded.metadata().status());

    // Duplicate create with identical data replays idempotently (client retry).
    transaction_blob_storage storage_a_retry;
    dt_test::make_prepared_storage(storage_a_retry, "mgr-a", {"pa"}, false, std::chrono::seconds{30});
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          transaction_manager::me()->create_transaction(ctx, std::move(storage_a_retry))));

    // --- LRU hit: a repeated mutable_transaction does not read the DB again
    transaction_metadata metadata_a;
    metadata_a.set_transaction_uuid("mgr-a");
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata_a, trans)));
    CASE_EXPECT_TRUE(!!trans);
    size_t db_reads_before = test.db().calls("distribute_transaction", op_type::kv_get_all);
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata_a, trans)));
    CASE_EXPECT_EQ(db_reads_before, test.db().calls("distribute_transaction", op_type::kv_get_all));

    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  if (!result.task_exited) {
    CASE_MSG_INFO() << "manager_lifecycle failed: " << result.diagnostic << '\n';
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(0, result.result_code);

  // DT-009 (part 1): a 30s transaction gets exactly ceil(30 + 5s grace) TTL.
  CASE_EXPECT_FALSE(ttl_values.empty());
  if (!ttl_values.empty()) {
    CASE_EXPECT_EQ(35, ttl_values.front());
  }

  // Second phase: capacity eviction + DB reload + TTL clamp values. mgr-b is created FIRST so the
  // later trio makes it the least recently used live entry; the tick after that must evict it.
  ttl_values.clear();
  auto task2 = test.run_task("manager_ttl_eviction", std::chrono::seconds{10}, [&test](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage storage_b;
    dt_test::make_prepared_storage(storage_b, "mgr-b", {"pa"});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_b))));

    // timeout far beyond transaction_max_ttl (30d): clamped to exactly 30d
    transaction_blob_storage storage_long;
    dt_test::make_prepared_storage(storage_long, "mgr-long", {"pa"}, false,
                                   std::chrono::seconds{40 * 24 * 3600});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_long))));

    // short timeout (1s): ceil(1 + 5) = 6
    transaction_blob_storage storage_short;
    dt_test::make_prepared_storage(storage_short, "mgr-short", {"pa"}, false, std::chrono::seconds{1});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_short))));

    // Already-expired entries fall back to the configured default timeout (10s): ceil(10 + 5) = 15
    transaction_blob_storage storage_expired;
    dt_test::make_prepared_storage(storage_expired, "mgr-expired-fallback", {"pa"}, false, std::chrono::seconds{-5});
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          transaction_manager::me()->create_transaction(ctx, std::move(storage_expired))));
    transaction_metadata fallback_metadata;
    fallback_metadata.set_transaction_uuid("mgr-expired-fallback");
    transaction_manager::transaction_ptr_type fallback_trans;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          transaction_manager::me()->mutable_transaction(ctx, fallback_metadata, fallback_trans)));
    CASE_EXPECT_LE(std::chrono::seconds{9},
                   protobuf_to_system_clock(fallback_trans->data_object.metadata().expire_timepoint()) -
                       atfw::util::time::time_utility::now());

    // --- capacity eviction (lru_max_cache_count = 2): three newer entries evict mgr-b
    int evicted = transaction_manager::me()->tick();
    CASE_EXPECT_GE(evicted, 1);

    // The evicted record reloads from the DB with the same blob content and CAS version.
    transaction_metadata metadata_b;
    metadata_b.set_transaction_uuid("mgr-b");
    transaction_manager::transaction_ptr_type trans_b;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata_b, trans_b)));
    CASE_EXPECT_TRUE(!!trans_b);
    CASE_EXPECT_EQ("mgr-b", trans_b->data_object.metadata().transaction_uuid());
    CASE_EXPECT_EQ(1, trans_b->data_version);
    RPC_RETURN_CODE(0);
  });
  auto result2 = test.wait(task2, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);

  // TTL observations in creation order: mgr-b (30s) -> 35, long -> 30d (2592000, the max_ttl
  // clamp), short -> 6, expired-fallback -> 15. The eviction-reload pass does not refresh TTL
  // (only create/save do).
  CASE_EXPECT_GE(ttl_values.size(), static_cast<size_t>(4));
  if (ttl_values.size() >= 4) {
    CASE_EXPECT_EQ(35, ttl_values[0]);
    CASE_EXPECT_EQ(2592000, ttl_values[1]);
    CASE_EXPECT_EQ(6, ttl_values[2]);
    CASE_EXPECT_EQ(15, ttl_values[3]);
  }

  ttl_rule.reset();

  // --- DT-007: memory_only keeps everything in the LRU without a single DB operation.
  auto task3 = test.run_task("manager_memory_only_dt007", std::chrono::seconds{10}, [&test](rpc::context& ctx) -> rpc::result_code_type {
    size_t db_ops_before = test.db().calls("distribute_transaction");

    transaction_blob_storage storage_m1;
    dt_test::make_prepared_storage(storage_m1, "mgr-mem-1", {"pa", "pb"}, true);
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_m1))));

    transaction_metadata metadata_m1;
    metadata_m1.set_transaction_uuid("mgr-mem-1");
    metadata_m1.set_memory_only(true);
    transaction_manager::transaction_ptr_type trans_m1;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata_m1, trans_m1)));
    CASE_EXPECT_TRUE(!!trans_m1);

    // try_commit stays in memory (DT-007: mutable cache without DB writes)
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans_m1)));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   trans_m1->data_object.metadata().status());

    // A partial participant ack stays in memory; the record is only dropped when every participant
    // acked the same direction.
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans_m1, "pa")));

    // No DB operation happened for the memory-only transaction.
    CASE_EXPECT_EQ(db_ops_before, test.db().calls("distribute_transaction"));

    // memory_only eviction by capacity is allowed (with a warning log): two newer memory-only
    // entries evict mgr-mem-1 and the record is gone for good.
    transaction_blob_storage storage_m2;
    dt_test::make_prepared_storage(storage_m2, "mgr-mem-2", {"pa"}, true);
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_m2))));
    transaction_blob_storage storage_m3;
    dt_test::make_prepared_storage(storage_m3, "mgr-mem-3", {"pa"}, true);
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_m3))));
    transaction_manager::me()->tick();  // evict down to capacity

    transaction_metadata metadata_m1_after;
    metadata_m1_after.set_transaction_uuid("mgr-mem-1");
    metadata_m1_after.set_memory_only(true);
    transaction_manager::transaction_ptr_type trans_m1_after;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata_m1_after,
                                                                                         trans_m1_after)));
    CASE_EXPECT_FALSE(!!trans_m1_after);
    RPC_RETURN_CODE(0);
  });
  auto result3 = test.wait(task3, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result3.task_exited);
  CASE_EXPECT_EQ(0, result3.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-019: expired PREPARED records are auto-rejected exactly once per save ============

CASE_TEST(component_dtcoordsvr, manager_timeout_reject_save_and_cache_invalidation_dt019) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  // Success path: the auto-reject decision is CAS-saved and survives a full cache loss.
  auto task = test.run_task("timeout_reject_success", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage expired_storage;
    dt_test::make_prepared_storage(expired_storage, "mgr-expired-1", {"pa"}, false, std::chrono::seconds{-10});
    uint64_t version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_write_record(ctx, "mgr-expired-1", expired_storage, version)));
    CASE_EXPECT_EQ(1, version);

    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-expired-1");
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
    CASE_EXPECT_TRUE(!!trans);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   trans->data_object.metadata().status());
    CASE_EXPECT_TRUE(trans->data_object.metadata().has_finish_timepoint());

    // The CAS save advanced the DB version; a fresh read (bypassing the LRU) still reports REJECTED.
    table_type record;
    uint64_t reloaded_version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-expired-1", record, reloaded_version)));
    CASE_EXPECT_EQ(2, reloaded_version);
    transaction_blob_storage reloaded;
    CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&reloaded));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   reloaded.metadata().status());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Failure path: seed the second expired record BEFORE arming the replace mock (the typed mock
  // intercepts the interface entry, so the seeding itself must run against the plain backend).
  auto seed_task = test.run_task("timeout_reject_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage expired_storage;
    dt_test::make_prepared_storage(expired_storage, "mgr-expired-2", {"pa"}, false, std::chrono::seconds{-10});
    uint64_t version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_write_record(ctx, "mgr-expired-2", expired_storage, version)));
    RPC_RETURN_CODE(0);
  });
  auto seed_result = test.wait(seed_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(seed_result.task_exited);
  CASE_EXPECT_EQ(0, seed_result.result_code);

  int replace_calls = 0;
  auto fail_rule = rpc::db::distribute_transaction::mock::replace(
      [&replace_calls](rpc::context&, const table_type&, rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        ++replace_calls;
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
      });
  CASE_EXPECT_TRUE(!!fail_rule);

  auto task2 = test.run_task("timeout_reject_save_failure", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-expired-2");
    transaction_manager::transaction_ptr_type trans;
    int32_t res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
    RPC_RETURN_CODE(0);
  });
  auto result2 = test.wait(task2, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);
  // One save attempt only: the manager does not retry the CAS internally.
  CASE_EXPECT_EQ(1, replace_calls);

  fail_rule.reset();

  auto task3 = test.run_task("timeout_reject_recovers_after_failure", std::chrono::seconds{10},
                             [](rpc::context& ctx) -> rpc::result_code_type {
    // After the failure the record re-loads from the DB (still PREPARED there) and the retry
    // succeeds: no cache-only REJECTED state may leak between calls.
    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-expired-2");
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
    CASE_EXPECT_TRUE(!!trans);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   trans->data_object.metadata().status());
    RPC_RETURN_CODE(0);
  });
  auto result3 = test.wait(task3, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result3.task_exited);
  CASE_EXPECT_EQ(0, result3.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ global terminal + participant ack matrix (5.1.7 / DT-018) + remove (DT-004) ============

CASE_TEST(component_dtcoordsvr, manager_terminal_and_participator_acks_dt018) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task = test.run_task("global_terminal_race", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    // Global commit is idempotent and sticky: the reverse decision cannot flip it.
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "mgr-race-1", {"pa"});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));

    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-race-1");
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   trans->data_object.metadata().status());
    CASE_EXPECT_GT(trans->data_object.metadata().finish_timepoint().seconds(), 0);

    // repeated commit is an idempotent success
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   trans->data_object.metadata().status());

    // reject after commit keeps the persisted COMMITED terminal (the client must follow it)
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, trans)));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   trans->data_object.metadata().status());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  auto task2 = test.run_task("participator_ack_matrix_dt018", std::chrono::seconds{10},
                             [](rpc::context& ctx) -> rpc::result_code_type {
    // Two participants, mixed directions: the record survives, no terminal flips.
    transaction_blob_storage mixed_storage;
    dt_test::make_prepared_storage(mixed_storage, "mgr-mixed-1", {"pa", "pb"});
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          transaction_manager::me()->create_transaction(ctx, std::move(mixed_storage))));

    transaction_metadata mixed_metadata;
    mixed_metadata.set_transaction_uuid("mgr-mixed-1");
    transaction_manager::transaction_ptr_type mixed_trans;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          transaction_manager::me()->mutable_transaction(ctx, mixed_metadata, mixed_trans)));

    // pa commits: the participant status advances while the global status stays PREPARED.
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, mixed_trans, "pa")));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   (*mixed_trans->data_object.mutable_participators())["pa"].participator_status());
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                   mixed_trans->data_object.metadata().status());

    // Reverse ack on pa: rejected because pa already reached COMMITED (a participant terminal is
    // not reversible either).
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, mixed_trans, "pa")));

    // pb rejects: mixed directions keep the record.
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, mixed_trans, "pb")));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   (*mixed_trans->data_object.mutable_participators())["pb"].participator_status());

    // Reverse ack on pb fails the same way.
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, mixed_trans, "pb")));

    // unknown participator key
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, mixed_trans, "pz")));

    // Mixed direction: the record still exists in the DB (TTL cleans it up later).
    table_type mixed_record;
    uint64_t mixed_version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-mixed-1", mixed_record, mixed_version)));
    CASE_EXPECT_GE(mixed_version, 2);

    // All-same-direction acks remove the record and the LRU entry.
    transaction_blob_storage all_storage;
    dt_test::make_prepared_storage(all_storage, "mgr-all-commit-1", {"pa", "pb"});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(all_storage))));
    transaction_metadata all_metadata;
    all_metadata.set_transaction_uuid("mgr-all-commit-1");
    transaction_manager::transaction_ptr_type all_trans;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          transaction_manager::me()->mutable_transaction(ctx, all_metadata, all_trans)));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, all_trans, "pa")));
    table_type intermediate_record;
    uint64_t intermediate_version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          db_read_record(ctx, "mgr-all-commit-1", intermediate_record, intermediate_version)));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, all_trans, "pb")));

    table_type gone_record;
    uint64_t gone_version = 0;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                   RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-all-commit-1", gone_record, gone_version)));
    RPC_RETURN_CODE(0);
  });
  auto result2 = test.wait(task2, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);

  // DT-004: remove_all DB failures propagate, the record stays recoverable and the success/notfound
  // paths clear everything.
  int remove_fail_calls = 0;
  auto remove_fail_rule = rpc::db::distribute_transaction::mock::remove_all(
      [&remove_fail_calls](rpc::context&, const table_type&, rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        ++remove_fail_calls;
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
      });
  CASE_EXPECT_TRUE(!!remove_fail_rule);

  auto task3 = test.run_task("remove_failure_dt004", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "mgr-remove-1", {"pa"});
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));

    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-remove-1");
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));

    // The record and its recoverable state survive the failed removal.
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(0,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
    CASE_EXPECT_TRUE(!!trans);
    RPC_RETURN_CODE(0);
  });
  auto result3 = test.wait(task3, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result3.task_exited);
  CASE_EXPECT_EQ(0, result3.result_code);
  CASE_EXPECT_EQ(1, remove_fail_calls);

  remove_fail_rule.reset();

  auto task4 = test.run_task("remove_recovers", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-remove-1");
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));

    table_type gone_record;
    uint64_t gone_version = 0;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                   RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-remove-1", gone_record, gone_version)));

    // try_remove on an absent record is an idempotent success (the DB miss maps to success).
    transaction_metadata absent_metadata;
    absent_metadata.set_transaction_uuid("mgr-remove-absent");
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, absent_metadata)));

    // empty UUID is rejected before any IO
    transaction_metadata empty_metadata;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, empty_metadata)));
    RPC_RETURN_CODE(0);
  });
  auto result4 = test.wait(task4, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result4.task_exited);
  CASE_EXPECT_EQ(0, result4.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 7 coordinator actions through the real SSMsg/task path ============

CASE_TEST(component_dtcoordsvr, actions_via_invoke_ss_action) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task = test.run_task("action_flow", std::chrono::seconds{20}, [](rpc::context& ctx) -> rpc::result_code_type {
    atfw::testing::ss_action_invoke_options invoke_options{rpc::transaction::packer::get_full_name_of_create()};
    invoke_options.source.node_id = 0x130001;
    invoke_options.source.node_name = "unit-test-action-source";
    invoke_options.source.source_task_id = 77;
    invoke_options.source.sequence = 1001;

    // create
    atfw::distributed_system::SSDistributeTransactionCreateReq request;
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "act-create-1", {"pa", "pb"});
    protobuf_copy_message(*request.mutable_storage(), storage);
    int32_t res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_create>(ctx, request, invoke_options));
    CASE_EXPECT_EQ(0, res);

    table_type record;
    uint64_t version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "act-create-1", record, version)));
    CASE_EXPECT_EQ("act-create-1", record.transaction_uuid());

    // query: existing and absent records both complete the task (business codes travel in the
    // response envelope, asserted by the raw dispatcher case below).
    atfw::distributed_system::SSDistributeTransactionQueryReq query_request;
    query_request.mutable_metadata()->set_transaction_uuid("act-create-1");
    atfw::testing::ss_action_invoke_options query_options{rpc::transaction::packer::get_full_name_of_query()};
    query_options.source.node_id = 0x130001;
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_query>(ctx, query_request, query_options));
    CASE_EXPECT_EQ(0, res);

    atfw::distributed_system::SSDistributeTransactionQueryReq absent_request;
    absent_request.mutable_metadata()->set_transaction_uuid("act-create-absent");
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_query>(ctx, absent_request, query_options));
    CASE_EXPECT_EQ(0, res);

    // commit drives the global terminal.
    atfw::distributed_system::SSDistributeTransactionCommitReq commit_request;
    commit_request.mutable_metadata()->set_transaction_uuid("act-create-1");
    atfw::testing::ss_action_invoke_options commit_options{rpc::transaction::packer::get_full_name_of_commit()};
    commit_options.source.node_id = 0x130001;
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_commit>(ctx, commit_request, commit_options));
    CASE_EXPECT_EQ(0, res);

    table_type committed_record;
    uint64_t committed_version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "act-create-1", committed_record, committed_version)));
    transaction_blob_storage committed_storage;
    CASE_EXPECT_TRUE(committed_record.blob_data().UnpackTo(&committed_storage));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   committed_storage.metadata().status());

    // participant ack actions remove the record once all participants acked the same direction.
    atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq ack_pa;
    ack_pa.mutable_metadata()->set_transaction_uuid("act-create-1");
    ack_pa.set_participator_key("pa");
    atfw::testing::ss_action_invoke_options ack_options{
        rpc::transaction::packer::get_full_name_of_commit_participator()};
    ack_options.source.node_id = 0x130001;
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_commit_participator>(ctx, ack_pa, ack_options));
    CASE_EXPECT_EQ(0, res);

    table_type intermediate_record;
    uint64_t intermediate_version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                          db_read_record(ctx, "act-create-1", intermediate_record, intermediate_version)));

    atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq ack_pb;
    ack_pb.mutable_metadata()->set_transaction_uuid("act-create-1");
    ack_pb.set_participator_key("pb");
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_commit_participator>(ctx, ack_pb, ack_options));
    CASE_EXPECT_EQ(0, res);

    table_type gone_record;
    uint64_t gone_version = 0;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                   RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "act-create-1", gone_record, gone_version)));

    // reject flow: create -> reject -> reject_participator removes the record.
    atfw::distributed_system::SSDistributeTransactionCreateReq reject_create_request;
    transaction_blob_storage reject_storage;
    dt_test::make_prepared_storage(reject_storage, "act-reject-1", {"pa"});
    protobuf_copy_message(*reject_create_request.mutable_storage(), reject_storage);
    res = RPC_AWAIT_CODE_RESULT(
        atfw::testing::invoke_ss_action<task_action_create>(ctx, reject_create_request, invoke_options));
    CASE_EXPECT_EQ(0, res);

    atfw::distributed_system::SSDistributeTransactionRejectReq reject_request;
    reject_request.mutable_metadata()->set_transaction_uuid("act-reject-1");
    atfw::testing::ss_action_invoke_options reject_options{rpc::transaction::packer::get_full_name_of_reject()};
    reject_options.source.node_id = 0x130001;
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_reject>(ctx, reject_request, reject_options));
    CASE_EXPECT_EQ(0, res);

    atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq reject_ack;
    reject_ack.mutable_metadata()->set_transaction_uuid("act-reject-1");
    reject_ack.set_participator_key("pa");
    atfw::testing::ss_action_invoke_options reject_ack_options{
        rpc::transaction::packer::get_full_name_of_reject_participator()};
    reject_ack_options.source.node_id = 0x130001;
    res = RPC_AWAIT_CODE_RESULT(
        atfw::testing::invoke_ss_action<task_action_reject_participator>(ctx, reject_ack, reject_ack_options));
    CASE_EXPECT_EQ(0, res);

    table_type reject_gone_record;
    uint64_t reject_gone_version = 0;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                   RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "act-reject-1", reject_gone_record, reject_gone_version)));

    // remove action with a wait response deletes an existing record.
    atfw::distributed_system::SSDistributeTransactionCreateReq remove_create_request;
    transaction_blob_storage remove_storage;
    dt_test::make_prepared_storage(remove_storage, "act-remove-1", {"pa"});
    protobuf_copy_message(*remove_create_request.mutable_storage(), remove_storage);
    res = RPC_AWAIT_CODE_RESULT(
        atfw::testing::invoke_ss_action<task_action_create>(ctx, remove_create_request, invoke_options));
    CASE_EXPECT_EQ(0, res);

    atfw::distributed_system::SSDistributeTransactionRemoveReq remove_request;
    remove_request.mutable_metadata()->set_transaction_uuid("act-remove-1");
    atfw::testing::ss_action_invoke_options remove_options{rpc::transaction::packer::get_full_name_of_remove()};
    remove_options.source.node_id = 0x130001;
    res = RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_remove>(ctx, remove_request, remove_options));
    CASE_EXPECT_EQ(0, res);

    table_type remove_gone_record;
    uint64_t remove_gone_version = 0;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                   RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "act-remove-1", remove_gone_record, remove_gone_version)));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{40});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ raw dispatcher: envelope-level behavior ============

CASE_TEST(component_dtcoordsvr, actions_raw_dispatcher_envelope) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto sender = test.discovery().add_node(dt_test::make_coordinator_node(0x1300F1, 1));
  CASE_EXPECT_TRUE(!!sender);
  if (!sender) {
    test.stop();
    return;
  }

  auto inject = [&test, &sender](gsl::string_view rpc_name, const google::protobuf::Message& body, uint64_t sequence) {
    atframework::SSMsg msg;
    auto* head = msg.mutable_head();
    head->set_node_id(0x1300F1);
    head->set_node_name("unit-test-raw-source");
    head->set_source_task_id(88);
    head->set_sequence(sequence);
    auto* rpc_request = head->mutable_rpc_request();
    rpc_request->set_rpc_name(rpc_name.data(), rpc_name.size());
    rpc_request->set_type_url(body.GetDescriptor()->full_name());
    CASE_EXPECT_TRUE(body.SerializeToString(msg.mutable_body_bin()));
    std::string payload;
    CASE_EXPECT_TRUE(msg.SerializeToString(&payload));
    CASE_EXPECT_EQ(0, test.transport().inject_inbound(
                          sender, static_cast<int32_t>(::atfw::component::message_type::kInServerMessage),
                          gsl::span<const unsigned char>{reinterpret_cast<const unsigned char*>(payload.data()),
                                                         payload.size()},
                          sequence));
    for (int i = 0; i < 6; ++i) {
      test.pump_once();
    }
  };

  size_t db_ops_before = test.db().calls("distribute_transaction");

  // Empty-UUID create: the action answers with an error envelope and never touches the DB.
  atfw::distributed_system::SSDistributeTransactionCreateReq empty_uuid_request;
  inject(rpc::transaction::packer::get_full_name_of_create(), empty_uuid_request, 2001);

  const atfw::testing::outbound_message* error_response = nullptr;
  for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
    const atfw::testing::outbound_message* record = test.transport().outbound_at(i);
    if (record != nullptr && record->target_node_id == 0x1300F1 && record->sequence == 2001) {
      error_response = record;
    }
  }
  CASE_EXPECT_TRUE(error_response != nullptr);
  if (error_response != nullptr) {
    atframework::SSMsg response_msg;
    CASE_EXPECT_TRUE(response_msg.ParseFromArray(error_response->payload.data(),
                                                 static_cast<int>(error_response->payload.size())));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, response_msg.head().error_code());
  }
  CASE_EXPECT_EQ(db_ops_before, test.db().calls("distribute_transaction"));

  // Unknown RPC name: the dispatcher rejects the envelope with an error response.
  atfw::distributed_system::SSDistributeTransactionQueryReq bogus_request;
  bogus_request.mutable_metadata()->set_transaction_uuid("raw-unknown");
  inject("atframework.distributed_system.DtcoordsvrService/not_exists", bogus_request, 2002);
  bool has_unknown_response = false;
  for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
    const atfw::testing::outbound_message* record = test.transport().outbound_at(i);
    if (record != nullptr && record->target_node_id == 0x1300F1 && record->sequence == 2002) {
      has_unknown_response = true;
      atframework::SSMsg response_msg;
      CASE_EXPECT_TRUE(response_msg.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size())));
      CASE_EXPECT_NE(0, response_msg.head().error_code());
    }
  }
  CASE_EXPECT_TRUE(has_unknown_response);

  CASE_EXPECT_EQ(0, test.stop());
}
