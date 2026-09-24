// Copyright 2026 atframework
//
// dtcoordsvr coordinator unit tests: transaction_manager
// public API (create/mutable/save/try_commit/try_reject/participant acks/try_remove/tick) plus the
// seven RPC actions. The manager is a process-lifetime singleton, so every case uses a unique UUID
// prefix and the LRU capacity assertions only rely on relative visit order, never on absolute
// leftover counts from earlier cases.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/dtcoordsvr_config.pb.h>
#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>
#include <atframework/testing/ss_action.h>

#include <common/file_system.h>

#include <std/explicit_declare.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app/handle_ss_rpc_dtcoordsvrservice.atfw.gen.h"
#include "dt_test_common.h"  // NOLINT(build/include_subdir)
#include "logic/action/task_action_commit.h"
#include "logic/action/task_action_commit_participator.h"
#include "logic/action/task_action_create.h"
#include "logic/action/task_action_query.h"
#include "logic/action/task_action_reject.h"
#include "logic/action/task_action_reject_participator.h"
#include "logic/action/task_action_remove.h"
#include "logic/transaction_manager.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/rpc_shared_message.h"
#include "rpc/rpc_utils.h"
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "utility/protobuf_mini_dumper.h"

namespace {
using atfw::distributed_system::EnDistibutedTransactionStatus;
using atfw::distributed_system::transaction_blob_storage;
using atfw::distributed_system::transaction_metadata;
using table_type = PROJECT_NAMESPACE_ID::table_distribute_transaction;
using op_type = rpc::db::hash_table::unit_test_request::op_type;

constexpr uint32_t kTestZoneId = 0;  // non-replication transactions live in zone 0

class coordinator_io_gate {
 public:
  rpc::result_code_type wait(rpc::context& ctx) {
    task_id_ = ctx.get_task_context().task_id;
    auto options = dispatcher_make_default<dispatcher_await_options>();
    options.sequence = 1;
    options.timeout = std::chrono::seconds{5};
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::custom_wait(ctx, this, options)));
  }

  bool entered() const { return task_id_ != 0; }

  int release() {
    auto data = dispatcher_make_default<dispatcher_resume_data_type>();
    data.sequence = 1;
    data.message.message_type = reinterpret_cast<uintptr_t>(this);
    return rpc::custom_resume(task_id_, data);
  }

 private:
  task_type_trait::id_type task_id_ = 0;
};

// dtcoordsvr.lru_max_cache_count 的用例级注入。字段带 CONFIGURE 注解（默认 200000）后，
// loader 里 set 的 C++ 预设值会被注解默认值覆盖，只有 env/yaml 配置源优先于注解默认值
// （parse_configures_into 的优先级：env > yaml > 注解默认值），因此用环境变量按用例注入。
class scoped_dtcoordsvr_capacity {
 public:
  explicit scoped_dtcoordsvr_capacity(uint32_t capacity) {
    std::string old = atfw::util::file_system::getenv(kEnvName);
    if (!old.empty()) {
      previous_ = old;
      has_previous_ = true;
    }
    set_env(kEnvName, std::to_string(capacity).c_str());
  }

  ~scoped_dtcoordsvr_capacity() {
    if (has_previous_) {
      set_env(kEnvName, previous_.c_str());
    } else {
#if defined(_WIN32) || defined(_WIN64)
      _putenv_s(kEnvName, "");
#else
      unsetenv(kEnvName);
#endif
    }
  }

  scoped_dtcoordsvr_capacity(const scoped_dtcoordsvr_capacity&) = delete;
  scoped_dtcoordsvr_capacity& operator=(const scoped_dtcoordsvr_capacity&) = delete;

 private:
  static void set_env(const char* name, const char* value) {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
  }

  static constexpr const char* kEnvName = "ATAPP_DTCOORDSVR_LRU_MAX_CACHE_COUNT";
  std::string previous_;
  bool has_previous_ = false;
};

// Register the same server-instance config loader as dtcoordsvr_main.cpp. Annotated fields
// (lru_expired_duration 60s, lru_max_cache_count 200000, grace 5s, max TTL 30d, default timeout 10s)
// are re-applied by parse_configures_into from the annotation defaults and are the effective values
// asserted below — setting them in C++ here would be silently discarded.
void setup_dtcoordsvr_config_loader() {
  logic_config::me()->set_server_instance_config_loader([](atfw::atapp::app& app_, logic_config&,
                                                           logic_config::server_instance_config_ptr& to) {
    auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::distributed_system::config::dtcoordsvr_cfg>();
    app_.parse_configures_into(*config_ptr, "dtcoordsvr", "ATAPP_DTCOORDSVR");
    to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
  });
}

// The rpc-unit-test runtime resets process-lifetime dispatcher registrations on every start, so the
// handles must be re-registered for each fixture.
void register_dtcoordsvr_handles() { handle::transaction::register_handles_for_dtcoordsvrservice(); }

rpc::result_code_type db_read_record(rpc::context& ctx, const std::string& uuid, table_type& output,
                                     uint64_t& version) {
  rpc::shared_message<table_type> storage{ctx};
  int32_t res =
      RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::get_all(ctx, kTestZoneId, uuid, *storage, version));
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

CASE_TEST(component_dtcoordsvr, evicted_handles_cannot_change_reloaded_transactions) {
  scoped_dtcoordsvr_capacity capacity_override{1};
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  auto task =
      test.run_task("evicted_handles", std::chrono::seconds{5}, [&test](rpc::context& ctx) -> rpc::result_code_type {
        for (bool memory_only : {false, true}) {
          const std::string uuid = memory_only ? "evicted-handle-memory" : "evicted-handle-persistent";
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, uuid, {"pa"}, memory_only);
          transaction_blob_storage replay;
          protobuf_copy_message(replay, storage);
          transaction_metadata metadata;
          protobuf_copy_message(metadata, storage.metadata());
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
          transaction_manager::transaction_ptr_type stale;
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, stale)));
          if (!CASE_EXPECT_TRUE(!!stale)) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          atfw::util::memory::weak_rc_ptr<transaction_manager::transaction_lru_map_type::value_cache_type> watcher =
              stale;

          transaction_blob_storage filler;
          dt_test::make_prepared_storage(filler, uuid + "-filler", {"pa"}, true);
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(filler))));
          CASE_EXPECT_GE(transaction_manager::me()->tick(), 1);
          CASE_EXPECT_TRUE(stale->removed);

          // Replay after memory eviction and a persistent refetch both install a fresh cache object.
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(replay))));
          transaction_manager::transaction_ptr_type current;
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, current)));
          if (!CASE_EXPECT_TRUE(!!current)) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          CASE_EXPECT_NE(stale.get(), current.get());
          const auto db_calls_before = test.db().calls("distribute_transaction");
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->save(ctx, stale)));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, stale)));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, stale)));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, stale, "pa")));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, stale, "pa")));
          CASE_EXPECT_EQ(db_calls_before, test.db().calls("distribute_transaction"));
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                         stale->data_object.metadata().status());
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                         stale->data_object.participators().at("pa").participator_status());
          CASE_EXPECT_FALSE(current->removed);
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                         current->data_object.metadata().status());
          stale.reset();
          CASE_EXPECT_TRUE(watcher.expired());

          // The current object still owns the original transaction and can complete normally.
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, current)));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, current, "pa")));
          CASE_EXPECT_TRUE(current->removed);
          transaction_manager::me()->clear_lru_for_unit_test();
        }
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  transaction_manager::me()->clear_lru_for_unit_test();
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, create_replay_preserves_terminal_and_acknowledgements) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task = test.run_task("create_replay", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    for (bool memory_only : {false, true}) {
      for (bool commit : {false, true}) {
        const std::string uuid =
            std::string("replay-terminal-") + (memory_only ? "memory-" : "db-") + (commit ? "commit" : "reject");
        transaction_blob_storage original;
        dt_test::make_prepared_storage(original, uuid, {"pa", "pb"}, memory_only);
        transaction_blob_storage request = original;
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request))));
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, original.metadata(), trans)));
        if (!CASE_EXPECT_TRUE(!!trans)) {
          RPC_RETURN_CODE(-1);
        }
        if (commit) {
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pa")));
        } else {
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, trans)));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, trans, "pa")));
        }
        const auto terminal = trans->data_object.metadata().status();
        const auto finish = trans->data_object.metadata().finish_timepoint();
        table_type record;
        uint64_t version_before = 0;
        if (!memory_only) {
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, uuid, record, version_before)));
          transaction_manager::me()->clear_lru_for_unit_test();
        }

        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(original))));
        transaction_metadata metadata = trans->data_object.metadata();
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        if (!CASE_EXPECT_TRUE(!!trans)) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(terminal, trans->data_object.metadata().status());
        CASE_EXPECT_EQ(finish.seconds(), trans->data_object.metadata().finish_timepoint().seconds());
        CASE_EXPECT_EQ(finish.nanos(), trans->data_object.metadata().finish_timepoint().nanos());
        auto participator = trans->data_object.participators().find("pa");
        if (CASE_EXPECT_TRUE(participator != trans->data_object.participators().end())) {
          CASE_EXPECT_EQ(terminal, participator->second.participator_status());
        }
        if (!memory_only) {
          uint64_t version_after = 0;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, uuid, record, version_after)));
          CASE_EXPECT_EQ(version_before, version_after);
        }
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
      }
    }
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, mismatched_stored_uuid_is_rejected_before_query_or_replay_can_write) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();
  auto task = test.run_task(
      "mismatched_stored_uuid", std::chrono::seconds{5}, [&test](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage corrupt_storage;
        dt_test::make_prepared_storage(corrupt_storage, "mismatched-blob-uuid", {"pa"});
        uint64_t version = 0;
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(db_write_record(ctx, "mismatched-record-key", corrupt_storage, version)));
        transaction_blob_storage request;
        dt_test::make_prepared_storage(request, "mismatched-record-key", {"pa"});
        transaction_manager::transaction_ptr_type output;
        CASE_EXPECT_EQ(
            PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK,
            RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, request.metadata(), output)));
        CASE_EXPECT_FALSE(!!output);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request))));
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        CASE_EXPECT_EQ(1, test.db().calls("distribute_transaction", op_type::kv_set));
        CASE_EXPECT_EQ(0, test.db().calls("distribute_transaction", op_type::set_ttl));
        table_type record;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mismatched-record-key", record, version)));
        CASE_EXPECT_EQ(1, version);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mismatched-blob-uuid", record, version)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, remove_uses_cached_persistence_mode_and_replication_zone) {
  atfw::testing::runtime test;
  auto options = make_dtcoordsvr_runtime_options();
  options.zone_id = 17;
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  auto task = test.run_task(
      "remove_cached_metadata", std::chrono::seconds{5}, [&test, &options](rpc::context& ctx) -> rpc::result_code_type {
        struct remove_case {
          const char* uuid;
          bool memory_only;
          bool replicated;
          bool request_memory_only;
          bool request_replicated;
        };
        for (const auto& scenario : {remove_case{"remove-persistent-hint", false, false, true, false},
                                     remove_case{"remove-memory-hint", true, false, false, false},
                                     remove_case{"remove-global-zone", false, false, false, true},
                                     remove_case{"remove-local-zone", false, true, false, false}}) {
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, scenario.uuid, {"pa"}, scenario.memory_only);
          if (scenario.replicated) {
            storage.mutable_metadata()->set_replicate_read_count(1);
            storage.mutable_metadata()->add_replicate_node_server_id(options.app_id);
          }
          transaction_metadata metadata = storage.metadata();
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
          transaction_manager::transaction_ptr_type retained;
          CASE_EXPECT_EQ(
              0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, retained)));
          if (!CASE_EXPECT_TRUE(!!retained)) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }

          transaction_metadata request;
          request.set_transaction_uuid(scenario.uuid);
          request.set_memory_only(scenario.request_memory_only);
          if (scenario.request_replicated) {
            request.set_replicate_read_count(1);
            request.add_replicate_node_server_id(options.app_id);
          }
          const auto db_calls_before = test.db().calls("distribute_transaction");
          const auto deletes_before = test.db().calls("distribute_transaction", op_type::remove_all);
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, request)));
          CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
          CASE_EXPECT_TRUE(retained->removed);
          if (scenario.memory_only) {
            CASE_EXPECT_EQ(db_calls_before, test.db().calls("distribute_transaction"));
          } else {
            CASE_EXPECT_EQ(deletes_before + 1, test.db().calls("distribute_transaction", op_type::remove_all));
            table_type record;
            uint64_t version = 0;
            const uint32_t stored_zone = scenario.replicated ? 17 : 0;
            CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                           RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::get_all(
                               ctx, stored_zone, scenario.uuid, record, version)));
          }
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->save(ctx, retained)));
          atfw::util::memory::weak_rc_ptr<transaction_manager::transaction_lru_map_type::value_cache_type> watcher =
              retained;
          retained.reset();
          CASE_EXPECT_TRUE(watcher.expired());
        }
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, remove_without_cached_record_cleans_failed_placeholder) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();
  int delete_calls = 0;
  auto rule = test.db().mock_table("distribute_transaction");
  rule.on(op_type::remove_all, [&delete_calls](atfw::testing::db_table_context& ctx) -> bool {
    if (++delete_calls == 1) {
      ctx.return_code = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
      return true;
    }
    return false;
  });
  CASE_EXPECT_TRUE(!!rule);
  auto task = test.run_task(
      "remove_without_cache", std::chrono::seconds{5}, [&delete_calls](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "remove-failed-placeholder", {"pa"});
        transaction_metadata metadata = storage.metadata();
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        transaction_manager::me()->clear_lru_for_unit_test();
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
        CASE_EXPECT_EQ(1, delete_calls);
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        table_type record;
        uint64_t version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, metadata.transaction_uuid(), record, version)));

        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
        CASE_EXPECT_EQ(2, delete_calls);
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, metadata.transaction_uuid(), record, version)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
        CASE_EXPECT_EQ(3, delete_calls);
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, remove_blocks_new_writes_until_database_delete_finishes) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  auto seed =
      test.run_task("remove_write_seed", std::chrono::seconds{5}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "remove-write-race", {"pa"});
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
      });
  auto seeded = test.wait(seed, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(seeded.task_exited);
  CASE_EXPECT_EQ(0, seeded.result_code);

  coordinator_io_gate remove_gate;
  coordinator_io_gate replace_gate;
  bool commit_started = false;
  bool commit_finished = false;
  rpc::unit_test::mock_rule_handle remove_rule;
  rpc::unit_test::mock_rule_handle replace_rule;
  remove_rule = rpc::db::distribute_transaction::mock::remove_all(
      [&remove_gate, &remove_rule](rpc::context& ctx, const table_type& input,
                                   rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        int result = RPC_AWAIT_CODE_RESULT(remove_gate.wait(ctx));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        remove_rule.reset();
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::db::distribute_transaction::remove_all(ctx, input.zone_id(), input.transaction_uuid())));
      });
  replace_rule = rpc::db::distribute_transaction::mock::replace(
      [&replace_gate, &replace_rule](rpc::context& ctx, const table_type& input,
                                     rpc::unit_test::db_mock_meta& metadata) -> rpc::result_code_type {
        int result = RPC_AWAIT_CODE_RESULT(replace_gate.wait(ctx));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        replace_rule.reset();
        rpc::shared_message<table_type> output{ctx};
        protobuf_copy_message(*output, input);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, output, metadata.version)));
      });
  CASE_EXPECT_TRUE(!!remove_rule);
  CASE_EXPECT_TRUE(!!replace_rule);

  auto removing =
      test.run_task("remove_write_remove", std::chrono::seconds{5}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("remove-write-race");
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&remove_gate]() { return remove_gate.entered(); }));
  auto committing = test.run_task(
      "remove_write_commit", std::chrono::seconds{5},
      [&commit_started, &commit_finished](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("remove-write-race");
        transaction_manager::transaction_ptr_type trans;
        commit_started = true;
        int result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
        if (result == 0) {
          result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans));
        }
        commit_finished = true;
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&commit_started]() { return commit_started; }));
  CASE_EXPECT_FALSE(commit_finished);
  CASE_EXPECT_FALSE(replace_gate.entered());

  CASE_EXPECT_EQ(0, remove_gate.release());
  auto removed = test.wait(removing, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(removed.task_exited);
  CASE_EXPECT_EQ(0, removed.result_code);
  if (replace_gate.entered()) {
    CASE_EXPECT_EQ(0, replace_gate.release());
  }
  auto committed = test.wait(committing, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(committed.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, committed.result_code);
  CASE_EXPECT_FALSE(replace_gate.entered());
  remove_rule.reset();
  replace_rule.reset();

  auto verify =
      test.run_task("remove_write_verify", std::chrono::seconds{5}, [](rpc::context& ctx) -> rpc::result_code_type {
        table_type output;
        uint64_t version = 0;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "remove-write-race", output, version)));
        RPC_RETURN_CODE(0);
      });
  auto verified = test.wait(verify, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(verified.task_exited);
  CASE_EXPECT_EQ(0, verified.result_code);
  CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, create_replay_restores_ttl_without_overwriting_record) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  std::vector<uint64_t> ttl_values;
  auto ttl_rule = rpc::db::distribute_transaction::mock::set_ttl(
      [&ttl_values](rpc::context&, const table_type&, uint64_t ttl,
                    rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        ttl_values.push_back(ttl);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ttl_rule);
  auto task =
      test.run_task("replay_missing_ttl", std::chrono::seconds{5}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "replay-missing-ttl", {"pa"});
        uint64_t version = 0;
        // Simulate a process stopping after the insert, before set_ttl completed.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_write_record(ctx, "replay-missing-ttl", storage, version)));
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        table_type record;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "replay-missing-ttl", record, version)));
        CASE_EXPECT_EQ(1, version);
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  if (CASE_EXPECT_EQ(1, ttl_values.size())) {
    CASE_EXPECT_EQ(35, ttl_values.front());
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, create_ttl_completion_blocks_replay_query_and_remove) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  for (int operation : {0, 1, 2}) {
    for (bool fail_ttl : {false, true}) {
      const std::string uuid = fail_ttl ? "create-gate-failed" : "create-gate-success";
      transaction_blob_storage original;
      dt_test::make_prepared_storage(original, uuid, {"pa"});
      coordinator_io_gate ttl_gate;
      rpc::unit_test::mock_rule_handle ttl_rule;
      ttl_rule = rpc::db::distribute_transaction::mock::set_ttl(
          [&ttl_gate, &ttl_rule, fail_ttl](rpc::context& ctx, const table_type& input, uint64_t ttl,
                                           rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
            int result = RPC_AWAIT_CODE_RESULT(ttl_gate.wait(ctx));
            if (result < 0) {
              RPC_RETURN_CODE(result);
            }
            ttl_rule.reset();
            if (fail_ttl) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
            }
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                rpc::db::distribute_transaction::set_ttl(ctx, input.zone_id(), input.transaction_uuid(), ttl)));
          });
      auto creating = test.run_task(
          "create_ttl_gate", std::chrono::seconds{5}, [&original](rpc::context& ctx) -> rpc::result_code_type {
            transaction_blob_storage request;
            protobuf_copy_message(request, original);
            RPC_RETURN_CODE(
                RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request))));
          });
      CASE_EXPECT_TRUE(dt_test::wait_for(test, [&ttl_gate]() { return ttl_gate.entered(); }));

      const auto db_calls_before = test.db().calls("distribute_transaction");
      bool concurrent_started = false;
      bool concurrent_finished = false;
      auto concurrent = test.run_task(
          "create_ttl_concurrent", std::chrono::seconds{5},
          [&original, &concurrent_started, &concurrent_finished, operation,
           fail_ttl](rpc::context& ctx) -> rpc::result_code_type {
            concurrent_started = true;
            int result = 0;
            if (operation == 0) {
              transaction_blob_storage replay;
              protobuf_copy_message(replay, original);
              result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(replay)));
            } else if (operation == 1) {
              transaction_manager::transaction_ptr_type output;
              result = RPC_AWAIT_CODE_RESULT(
                  transaction_manager::me()->mutable_transaction(ctx, original.metadata(), output));
              CASE_EXPECT_EQ(!fail_ttl, !!output);
              if (output) {
                CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                               output->data_object.metadata().status());
              }
            } else {
              transaction_metadata request = original.metadata();
              request.set_memory_only(true);
              result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, request));
            }
            concurrent_finished = true;
            RPC_RETURN_CODE(result);
          });
      CASE_EXPECT_TRUE(dt_test::wait_for(test, [&concurrent_started]() { return concurrent_started; }));
      CASE_EXPECT_FALSE(concurrent_finished);
      CASE_EXPECT_EQ(db_calls_before, test.db().calls("distribute_transaction"));
      CASE_EXPECT_EQ(0, ttl_gate.release());
      auto created = test.wait(creating, std::chrono::seconds{10});
      CASE_EXPECT_TRUE(created.task_exited);
      CASE_EXPECT_EQ(fail_ttl ? PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT : 0, created.result_code);
      auto unblocked = test.wait(concurrent, std::chrono::seconds{10});
      CASE_EXPECT_TRUE(unblocked.task_exited);
      CASE_EXPECT_EQ(operation == 1 && fail_ttl ? PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND : 0,
                     unblocked.result_code);

      auto verify = test.run_task(
          "create_ttl_replay", std::chrono::seconds{5},
          [&original, fail_ttl, operation](rpc::context& ctx) -> rpc::result_code_type {
            if ((fail_ttl && operation != 0) || operation == 2) {
              table_type record;
              uint64_t version = 0;
              CASE_EXPECT_EQ(
                  PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                  RPC_AWAIT_CODE_RESULT(db_read_record(ctx, original.metadata().transaction_uuid(), record, version)));
              CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
            }
            transaction_blob_storage replay;
            protobuf_copy_message(replay, original);
            CASE_EXPECT_EQ(
                0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(replay))));
            transaction_manager::transaction_ptr_type output;
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(
                                  transaction_manager::me()->mutable_transaction(ctx, original.metadata(), output)));
            if (CASE_EXPECT_TRUE(!!output)) {
              CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                             output->data_object.metadata().status());
            }
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, original.metadata())));
            RPC_RETURN_CODE(0);
          });
      auto verified = test.wait(verify, std::chrono::seconds{10});
      CASE_EXPECT_TRUE(verified.task_exited);
      CASE_EXPECT_EQ(0, verified.result_code);
    }
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, remove_drains_writes_under_capacity_pressure_and_stops_queued_saves) {
  scoped_dtcoordsvr_capacity capacity_override{1};
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  for (bool evict : {false, true}) {
    const std::string uuid = evict ? "remove-evicted-save" : "remove-queued-save";
    transaction_manager::transaction_ptr_type trans;
    auto seed = test.run_task(
        "remove_pending_seed", std::chrono::seconds{5}, [&trans, &uuid](rpc::context& ctx) -> rpc::result_code_type {
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, uuid, {"pa", "pb"});
          transaction_metadata metadata = storage.metadata();
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        });
    auto seeded = test.wait(seed, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(seeded.task_exited);
    CASE_EXPECT_EQ(0, seeded.result_code);
    if (!CASE_EXPECT_TRUE(!!trans)) {
      break;
    }
    atfw::util::memory::weak_rc_ptr<transaction_manager::transaction_lru_map_type::value_cache_type> watcher = trans;
    coordinator_io_gate save_gate;
    coordinator_io_gate remove_gate;
    rpc::unit_test::mock_rule_handle replace_rule;
    replace_rule = rpc::db::distribute_transaction::mock::replace(
        [&save_gate, &replace_rule](rpc::context& ctx, const table_type& input,
                                    rpc::unit_test::db_mock_meta& metadata) -> rpc::result_code_type {
          int result = RPC_AWAIT_CODE_RESULT(save_gate.wait(ctx));
          if (result < 0) {
            RPC_RETURN_CODE(result);
          }
          replace_rule.reset();
          rpc::shared_message<table_type> storage{ctx};
          protobuf_copy_message(*storage, input);
          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, storage, metadata.version)));
        });
    auto saving = test.run_task(
        "remove_pending_save", std::chrono::seconds{5}, [&trans](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
        });
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&save_gate]() { return save_gate.entered(); }));

    if (evict) {
      auto eviction = test.run_task(
          "remove_pending_evict", std::chrono::seconds{5}, [](rpc::context& ctx) -> rpc::result_code_type {
            transaction_blob_storage filler;
            dt_test::make_prepared_storage(filler, "remove-pending-filler", {"pa"}, true);
            CASE_EXPECT_EQ(
                0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(filler))));
            CASE_EXPECT_EQ(1, transaction_manager::me()->tick());
            RPC_RETURN_CODE(0);
          });
      auto evicted = test.wait(eviction, std::chrono::seconds{10});
      CASE_EXPECT_TRUE(evicted.task_exited);
      CASE_EXPECT_EQ(0, evicted.result_code);
      CASE_EXPECT_FALSE(trans->removed);

      // A timed-out remover must leave the pending write available to the next remover.
      auto interrupted = test.run_task(
          "remove_pending_timeout", std::chrono::milliseconds{50}, [&uuid](rpc::context& ctx) -> rpc::result_code_type {
            transaction_metadata metadata;
            metadata.set_transaction_uuid(uuid);
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
          });
      auto interruption = test.wait(interrupted, std::chrono::seconds{2});
      CASE_EXPECT_TRUE(interruption.task_exited);
      CASE_EXPECT_TRUE(interruption.task_timed_out);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, interruption.result_code);
    }

    rpc::unit_test::mock_rule_handle remove_rule;
    remove_rule = rpc::db::distribute_transaction::mock::remove_all(
        [&remove_gate, &remove_rule](rpc::context& ctx, const table_type& input,
                                     rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
          int result = RPC_AWAIT_CODE_RESULT(remove_gate.wait(ctx));
          if (result < 0) {
            RPC_RETURN_CODE(result);
          }
          remove_rule.reset();
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
              rpc::db::distribute_transaction::remove_all(ctx, input.zone_id(), input.transaction_uuid())));
        });
    bool remove_started = false;
    bool remove_finished = false;
    auto removing =
        test.run_task("remove_pending_delete", std::chrono::seconds{5},
                      [&uuid, &remove_started, &remove_finished](rpc::context& ctx) -> rpc::result_code_type {
                        transaction_metadata metadata;
                        metadata.set_transaction_uuid(uuid);
                        remove_started = true;
                        int result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata));
                        remove_finished = true;
                        RPC_RETURN_CODE(result);
                      });
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&remove_started]() { return remove_started; }));
    CASE_EXPECT_FALSE(remove_finished);
    CASE_EXPECT_EQ(0, save_gate.release());
    auto saved = test.wait(saving, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(saved.task_exited);
    CASE_EXPECT_EQ(0, saved.result_code);
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&remove_gate]() { return remove_gate.entered(); }));
    bool queued_started = false;
    bool queued_finished = false;
    auto queued =
        test.run_task("remove_pending_ack", std::chrono::seconds{5},
                      [&trans, &queued_started, &queued_finished](rpc::context& ctx) -> rpc::result_code_type {
                        queued_started = true;
                        int result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pa"));
                        queued_finished = true;
                        RPC_RETURN_CODE(result);
                      });
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&queued_started]() { return queued_started; }));

    CASE_EXPECT_FALSE(queued_finished);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                   trans->data_object.participators().at("pa").participator_status());
    CASE_EXPECT_EQ(0, remove_gate.release());
    auto queued_result = test.wait(queued, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(queued_result.task_exited);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, queued_result.result_code);
    auto removed = test.wait(removing, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(removed.task_exited);
    CASE_EXPECT_EQ(0, removed.result_code);
    CASE_EXPECT_TRUE(trans->removed);
    trans.reset();
    CASE_EXPECT_TRUE(watcher.expired());

    auto verify = test.run_task(
        "remove_pending_verify", std::chrono::seconds{5}, [&uuid](rpc::context& ctx) -> rpc::result_code_type {
          table_type output;
          uint64_t version = 0;
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                         RPC_AWAIT_CODE_RESULT(db_read_record(ctx, uuid, output, version)));
          // A repeated removal also proves its transient UUID registration was released.
          transaction_metadata metadata;
          metadata.set_transaction_uuid(uuid);
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, metadata)));
        });
    auto verified = test.wait(verify, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(verified.task_exited);
    CASE_EXPECT_EQ(0, verified.result_code);
    transaction_manager::me()->clear_lru_for_unit_test();
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, caller_timeout_keeps_io_until_following_operation_finishes) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  // Each caller times out while its own DB operation is suspended. The independent IO must remain discoverable.
  for (int operation : {0, 1, 2, 3}) {
    const std::string uuid = "caller-timeout-" + std::to_string(operation);
    transaction_blob_storage original;
    dt_test::make_prepared_storage(original, uuid, {"pa", "pb"});
    transaction_manager::transaction_ptr_type retained;
    if (operation != 0) {
      auto seed = test.run_task("caller_timeout_seed", std::chrono::seconds{5},
                                [&original, &retained](rpc::context& ctx) -> rpc::result_code_type {
                                  transaction_blob_storage request = original;
                                  CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(
                                                        ctx, std::move(request))));
                                  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(
                                      ctx, original.metadata(), retained)));
                                });
      auto seeded = test.wait(seed, std::chrono::seconds{10});
      CASE_EXPECT_TRUE(seeded.task_exited);
      CASE_EXPECT_EQ(0, seeded.result_code);
      if (!CASE_EXPECT_TRUE(!!retained)) {
        break;
      }
      if (operation == 1) {
        transaction_manager::me()->clear_lru_for_unit_test();
        retained.reset();
      }
    }

    coordinator_io_gate gate;
    rpc::unit_test::mock_rule_handle rule;
    if (operation == 0) {
      rule = rpc::db::distribute_transaction::mock::set_ttl(
          [&gate, &rule](rpc::context& ctx, const table_type& input, uint64_t ttl,
                         rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
            int result = RPC_AWAIT_CODE_RESULT(gate.wait(ctx));
            if (result < 0) {
              RPC_RETURN_CODE(result);
            }
            rule.reset();
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                rpc::db::distribute_transaction::set_ttl(ctx, input.zone_id(), input.transaction_uuid(), ttl)));
          });
    } else if (operation == 1) {
      rule = rpc::db::distribute_transaction::mock::get_all(
          [&gate, &rule](rpc::context& ctx, const table_type& input, table_type& output,
                         rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
            int result = RPC_AWAIT_CODE_RESULT(gate.wait(ctx));
            if (result < 0) {
              RPC_RETURN_CODE(result);
            }
            rule.reset();
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::get_all(
                ctx, input.zone_id(), input.transaction_uuid(), output, meta.version)));
          });
    } else if (operation == 2) {
      rule = rpc::db::distribute_transaction::mock::replace(
          [&gate, &rule](rpc::context& ctx, const table_type& input,
                         rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
            int result = RPC_AWAIT_CODE_RESULT(gate.wait(ctx));
            if (result < 0) {
              RPC_RETURN_CODE(result);
            }
            rule.reset();
            rpc::shared_message<table_type> output{ctx};
            protobuf_copy_message(*output, input);
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, output, meta.version)));
          });
    } else {
      rule = rpc::db::distribute_transaction::mock::remove_all(
          [&gate, &rule](rpc::context& ctx, const table_type& input,
                         rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
            int result = RPC_AWAIT_CODE_RESULT(gate.wait(ctx));
            if (result < 0) {
              RPC_RETURN_CODE(result);
            }
            rule.reset();
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                rpc::db::distribute_transaction::remove_all(ctx, input.zone_id(), input.transaction_uuid())));
          });
    }
    CASE_EXPECT_TRUE(!!rule);
    auto caller = test.run_task(
        "caller_timeout_io", std::chrono::milliseconds{50},
        [&original, &retained, operation](rpc::context& ctx) -> rpc::result_code_type {
          if (operation == 0) {
            transaction_blob_storage request = original;
            RPC_RETURN_CODE(
                RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request))));
          }
          if (operation == 1) {
            transaction_manager::transaction_ptr_type output;
            int result =
                RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, original.metadata(), output));
            CASE_EXPECT_FALSE(!!output);
            RPC_RETURN_CODE(result);
          }
          if (operation == 2) {
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, retained)));
          }
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, original.metadata())));
        });
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&gate]() { return gate.entered(); }));
    auto expired = test.wait(caller, std::chrono::seconds{2});
    CASE_EXPECT_TRUE(expired.task_exited);
    CASE_EXPECT_TRUE(expired.task_timed_out);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, expired.result_code);
    CASE_EXPECT_EQ(1, transaction_manager::me()->get_lru_size_for_unit_test());
    if (retained) {
      CASE_EXPECT_FALSE(retained->removed);
      CASE_EXPECT_FALSE(task_type_trait::empty(retained->io_task));
      CASE_EXPECT_FALSE(task_type_trait::is_exiting(retained->io_task));
    }

    bool following_started = false;
    bool following_finished = false;
    const auto calls_before = test.db().calls("distribute_transaction");
    auto following = test.run_task(
        "caller_timeout_following", std::chrono::seconds{5},
        [&original, &following_started, &following_finished, operation](rpc::context& ctx) -> rpc::result_code_type {
          following_started = true;
          int result = 0;
          if (operation == 3) {
            transaction_blob_storage request = original;
            result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request)));
          } else {
            result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, original.metadata()));
          }
          following_finished = true;
          RPC_RETURN_CODE(result);
        });
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&following_started]() { return following_started; }));
    CASE_EXPECT_FALSE(following_finished);
    CASE_EXPECT_EQ(calls_before, test.db().calls("distribute_transaction"));
    CASE_EXPECT_EQ(0, gate.release());
    auto followed = test.wait(following, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(followed.task_exited);
    CASE_EXPECT_EQ(0, followed.result_code);
    if (retained) {
      CASE_EXPECT_TRUE(retained->removed);
      CASE_EXPECT_TRUE(task_type_trait::empty(retained->io_task));
      atfw::util::memory::weak_rc_ptr<transaction_manager::transaction_lru_map_type::value_cache_type> watcher =
          retained;
      retained.reset();
      CASE_EXPECT_TRUE(watcher.expired());
    }
    auto verify = test.run_task(
        "caller_timeout_verify", std::chrono::seconds{5},
        [&original, operation](rpc::context& ctx) -> rpc::result_code_type {
          table_type record;
          uint64_t version = 0;
          int result =
              RPC_AWAIT_CODE_RESULT(db_read_record(ctx, original.metadata().transaction_uuid(), record, version));
          CASE_EXPECT_EQ(operation == 3 ? 0 : PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, result);
          if (operation == 3) {
            transaction_blob_storage persisted;
            CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&persisted));
            CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                           persisted.metadata().status());
            CASE_EXPECT_EQ(1, version);
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, original.metadata())));
          }
          RPC_RETURN_CODE(0);
        });
    auto verified = test.wait(verify, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(verified.task_exited);
    CASE_EXPECT_EQ(0, verified.result_code);
    CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, queued_ack_keeps_original_handle_and_query_waits_for_next_save) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();
  transaction_manager::transaction_ptr_type original;
  transaction_manager::transaction_ptr_type replacement;
  auto seed = test.run_task(
      "queued_ack_seed", std::chrono::seconds{5},
      [&original, &replacement](rpc::context& ctx) -> rpc::result_code_type {
        for (bool replace : {false, true}) {
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, replace ? "queued-ack-replacement" : "queued-ack-original",
                                         {"pa", "pb"}, replace);
          transaction_metadata metadata = storage.metadata();
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(
                                ctx, metadata, replace ? replacement : original)));
        }
        RPC_RETURN_CODE(0);
      });
  auto seeded = test.wait(seed, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(seeded.task_exited);
  CASE_EXPECT_EQ(0, seeded.result_code);
  if (!CASE_EXPECT_TRUE(!!original && !!replacement)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }

  coordinator_io_gate first_save;
  coordinator_io_gate next_save;
  rpc::unit_test::mock_rule_handle ttl_rule;
  ttl_rule = rpc::db::distribute_transaction::mock::set_ttl(
      [&first_save, &ttl_rule](rpc::context& ctx, const table_type& input, uint64_t ttl,
                               rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        int result = RPC_AWAIT_CODE_RESULT(first_save.wait(ctx));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        ttl_rule.reset();
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::db::distribute_transaction::set_ttl(ctx, input.zone_id(), input.transaction_uuid(), ttl)));
      });
  auto committing = test.run_task(
      "queued_ack_commit", std::chrono::seconds{5}, [&original](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, original)));
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&first_save]() { return first_save.entered(); }));

  rpc::unit_test::mock_rule_handle replace_rule;
  replace_rule = rpc::db::distribute_transaction::mock::replace(
      [&next_save, &replace_rule](rpc::context& ctx, const table_type& input,
                                  rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
        int result = RPC_AWAIT_CODE_RESULT(next_save.wait(ctx));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        replace_rule.reset();
        rpc::shared_message<table_type> output{ctx};
        protobuf_copy_message(*output, input);
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, output, meta.version)));
      });
  auto reusable = original;
  bool ack_started = false;
  auto acknowledging = test.run_task(
      "queued_ack_apply", std::chrono::seconds{5},
      [&reusable, &ack_started](rpc::context& ctx) -> rpc::result_code_type {
        ack_started = true;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, reusable, "pa")));
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&ack_started]() { return ack_started; }));
  reusable = replacement;
  // Release the first save so the queued ack applies to the original handle and registers the next
  // save before the query starts. Multiple co_await callers of the same task resume in an
  // unspecified order (libcopp promise_caller_manager keeps callers in a hash set), so a query that
  // was already waiting could observe the empty io_task window before the ack registers its save.
  CASE_EXPECT_EQ(0, first_save.release());
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&next_save]() { return next_save.entered(); }));
  CASE_EXPECT_FALSE(task_type_trait::empty(original->io_task));
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                 replacement->data_object.participators().at("pa").participator_status());
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                 replacement->data_object.metadata().status());
  bool query_started = false;
  bool query_finished = false;
  auto querying = test.run_task(
      "queued_ack_query", std::chrono::seconds{5},
      [&original, &query_started, &query_finished](rpc::context& ctx) -> rpc::result_code_type {
        query_started = true;
        transaction_metadata metadata = original->data_object.metadata();
        transaction_manager::transaction_ptr_type output;
        int result = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, output));
        query_finished = true;
        if (CASE_EXPECT_TRUE(!!output)) {
          CASE_EXPECT_EQ(original.get(), output.get());
          CASE_EXPECT_EQ(3, output->data_version);
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                         output->data_object.participators().at("pa").participator_status());
        }
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&query_started]() { return query_started; }));
  CASE_EXPECT_FALSE(query_finished);
  CASE_EXPECT_EQ(0, next_save.release());
  for (auto* task : {&committing, &acknowledging, &querying}) {
    auto result = test.wait(*task, std::chrono::seconds{10});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  auto verify = test.run_task(
      "queued_ack_verify", std::chrono::seconds{5}, [&original](rpc::context& ctx) -> rpc::result_code_type {
        table_type record;
        uint64_t version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "queued-ack-original", record, version)));
        CASE_EXPECT_EQ(3, version);
        transaction_blob_storage persisted;
        CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&persisted));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                       persisted.participators().at("pa").participator_status());
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, original, "pb")));
      });
  auto verified = test.wait(verify, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(verified.task_exited);
  CASE_EXPECT_EQ(0, verified.result_code);
  CASE_EXPECT_TRUE(original->removed);
  transaction_manager::me()->clear_lru_for_unit_test();
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_dtcoordsvr, io_task_start_failure_releases_placeholder_and_allows_retry) {
  atfw::testing::runtime test;
  CASE_EXPECT_EQ(0, test.start(make_dtcoordsvr_runtime_options()));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();
  auto task =
      test.run_task("io_start_failure", std::chrono::seconds{5}, [&test](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage original;
        dt_test::make_prepared_storage(original, "io-start-failure", {"pa"});
        auto rule =
            rpc::unit_test::mock_async_invoke([](gsl::string_view name, std::chrono::system_clock::duration&) -> int {
              return name == "transaction_manager.io" ? PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC : 0;
            });
        CASE_EXPECT_TRUE(!!rule);
        auto calls_before = test.db().calls("distribute_transaction");
        transaction_blob_storage request = original;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request))));
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        transaction_manager::transaction_ptr_type output;
        CASE_EXPECT_EQ(
            PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC,
            RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, original.metadata(), output)));
        CASE_EXPECT_FALSE(!!output);
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, original.metadata())));
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        CASE_EXPECT_EQ(calls_before, test.db().calls("distribute_transaction"));
        rule.reset();
        request = original;
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(request))));
        transaction_manager::me()->clear_lru_for_unit_test();
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, original.metadata(), output)));
        CASE_EXPECT_TRUE(!!output);
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_remove(ctx, original.metadata())));
        CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{10});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ create / mutable / TTL / eviction / DT-007 / DT-009 / DT-019 ============

CASE_TEST(component_dtcoordsvr, manager_create_query_ttl_and_eviction) {
  // 容量淘汰断言需要小容量：注入 lru_max_cache_count=2（注解默认 200000 不会淘汰）
  scoped_dtcoordsvr_capacity capacity_override{2};
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

  auto task =
      test.run_task("manager_lifecycle", std::chrono::seconds{10}, [&test](rpc::context& ctx) -> rpc::result_code_type {
        // --- parameter guards: empty uuid / no participators never touch the DB or LRU
        size_t db_ops_before = test.db().calls("distribute_transaction");

        transaction_blob_storage invalid_storage;
        dt_test::make_prepared_storage(invalid_storage, "", {"pa"});
        CASE_EXPECT_EQ(
            PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
            RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(invalid_storage))));

        transaction_blob_storage no_participator_storage;
        dt_test::make_prepared_storage(no_participator_storage, "mgr-empty-participators", {});
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                       RPC_AWAIT_CODE_RESULT(
                           transaction_manager::me()->create_transaction(ctx, std::move(no_participator_storage))));
        CASE_EXPECT_EQ(db_ops_before, test.db().calls("distribute_transaction"));

        // --- create inserts the DB record once and fills the LRU
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
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_a_retry))));

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
  auto task2 =
      test.run_task("manager_ttl_eviction", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage_b;
        dt_test::make_prepared_storage(storage_b, "mgr-b", {"pa"});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_b))));

        // timeout far beyond transaction_max_ttl (30d): clamped to exactly 30d
        transaction_blob_storage storage_long;
        dt_test::make_prepared_storage(storage_long, "mgr-long", {"pa"}, false, std::chrono::seconds{40 * 24 * 3600});
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_long))));

        // short timeout (1s): ceil(1 + 5) = 6
        transaction_blob_storage storage_short;
        dt_test::make_prepared_storage(storage_short, "mgr-short", {"pa"}, false, std::chrono::seconds{1});
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_short))));

        // Already-expired entries fall back to the configured default timeout (10s): ceil(10 + 5) = 15
        transaction_blob_storage storage_expired;
        dt_test::make_prepared_storage(storage_expired, "mgr-expired-fallback", {"pa"}, false,
                                       std::chrono::seconds{-5});
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_expired))));
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
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_EQ(35, ttl_values[0]);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_EQ(2592000, ttl_values[1]);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_EQ(6, ttl_values[2]);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_EQ(15, ttl_values[3]);
  }

  ttl_rule.reset();

  // --- DT-007: memory_only keeps everything in the LRU without a single DB operation.
  auto task3 = test.run_task(
      "manager_memory_only_dt007", std::chrono::seconds{10}, [&test](rpc::context& ctx) -> rpc::result_code_type {
        size_t db_ops_before = test.db().calls("distribute_transaction");

        transaction_blob_storage storage_m1;
        dt_test::make_prepared_storage(storage_m1, "mgr-mem-1", {"pa", "pb"}, true);
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_m1))));

        transaction_metadata metadata_m1;
        metadata_m1.set_transaction_uuid("mgr-mem-1");
        metadata_m1.set_memory_only(true);
        transaction_manager::transaction_ptr_type trans_m1;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata_m1, trans_m1)));
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
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_m2))));
        transaction_blob_storage storage_m3;
        dt_test::make_prepared_storage(storage_m3, "mgr-mem-3", {"pa"}, true);
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage_m3))));
        transaction_manager::me()->tick();  // evict down to capacity

        transaction_metadata metadata_m1_after;
        metadata_m1_after.set_transaction_uuid("mgr-mem-1");
        metadata_m1_after.set_memory_only(true);
        transaction_manager::transaction_ptr_type trans_m1_after;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                       RPC_AWAIT_CODE_RESULT(
                           transaction_manager::me()->mutable_transaction(ctx, metadata_m1_after, trans_m1_after)));
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
  auto task =
      test.run_task("timeout_reject_success", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage expired_storage;
        dt_test::make_prepared_storage(expired_storage, "mgr-expired-1", {"pa"}, false, std::chrono::seconds{-10});
        uint64_t version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_write_record(ctx, "mgr-expired-1", expired_storage, version)));
        CASE_EXPECT_EQ(1, version);

        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-expired-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
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
  auto seed_task =
      test.run_task("timeout_reject_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
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

  auto task2 = test.run_task(
      "timeout_reject_save_failure", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-expired-2");
        transaction_manager::transaction_ptr_type trans;
        int32_t res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
        CASE_EXPECT_FALSE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto result2 = test.wait(task2, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);
  // One save attempt only: the manager does not retry the CAS internally.
  CASE_EXPECT_EQ(1, replace_calls);

  fail_rule.reset();

  auto task3 = test.run_task(
      "timeout_reject_recovers_after_failure", std::chrono::seconds{10},
      [](rpc::context& ctx) -> rpc::result_code_type {
        // After the failure the record re-loads from the DB (still PREPARED there) and the retry
        // succeeds: no cache-only REJECTED state may leak between calls.
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-expired-2");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                       trans->data_object.metadata().status());
        RPC_RETURN_CODE(0);
      });
  auto result3 = test.wait(task3, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result3.task_exited);
  CASE_EXPECT_EQ(0, result3.result_code);

  // Boundary: expired but still within transaction_expire_grace_duration (effective 5s) is NOT
  // auto-rejected — the coordinator waits for the participator's own resolve first.
  auto task4 = test.run_task(
      "timeout_within_grace_stays_prepared", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage grace_storage;
        dt_test::make_prepared_storage(grace_storage, "mgr-grace-1", {"pa"}, false, std::chrono::seconds{-2});
        uint64_t version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_write_record(ctx, "mgr-grace-1", grace_storage, version)));
        CASE_EXPECT_EQ(1, version);

        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-grace-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                       trans->data_object.metadata().status());
        CASE_EXPECT_FALSE(trans->data_object.metadata().has_finish_timepoint());

        // No auto-reject save happened: a fresh DB read still reports version 1 and PREPARED.
        table_type record;
        uint64_t reloaded_version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-grace-1", record, reloaded_version)));
        CASE_EXPECT_EQ(1, reloaded_version);
        transaction_blob_storage reloaded;
        CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&reloaded));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                       reloaded.metadata().status());
        RPC_RETURN_CODE(0);
      });
  auto result4 = test.wait(task4, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result4.task_exited);
  CASE_EXPECT_EQ(0, result4.result_code);

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

  auto task =
      test.run_task("global_terminal_race", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        // Global commit is idempotent and sticky: the reverse decision cannot flip it.
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-race-1", {"pa"});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));

        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-race-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
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

  auto task2 = test.run_task(
      "participator_ack_matrix_dt018", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        // Two participants, mixed directions: the record survives, no terminal flips.
        transaction_blob_storage mixed_storage;
        dt_test::make_prepared_storage(mixed_storage, "mgr-mixed-1", {"pa", "pb"});
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(mixed_storage))));

        transaction_metadata mixed_metadata;
        mixed_metadata.set_transaction_uuid("mgr-mixed-1");
        transaction_manager::transaction_ptr_type mixed_trans;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, mixed_metadata, mixed_trans)));

        // pa commits: the participant status advances while the global status stays PREPARED.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, mixed_trans, "pa")));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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
                       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(all_storage))));
        transaction_metadata all_metadata;
        all_metadata.set_transaction_uuid("mgr-all-commit-1");
        transaction_manager::transaction_ptr_type all_trans;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, all_metadata, all_trans)));
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

  auto task3 =
      test.run_task("remove_failure_dt004", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
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
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto result3 = test.wait(task3, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result3.task_exited);
  CASE_EXPECT_EQ(0, result3.result_code);
  CASE_EXPECT_EQ(1, remove_fail_calls);

  remove_fail_rule.reset();

  auto task4 =
      test.run_task("remove_recovers", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
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
    int32_t res =
        RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_create>(ctx, request, invoke_options));
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
    res =
        RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_commit>(ctx, commit_request, commit_options));
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
    res = RPC_AWAIT_CODE_RESULT(
        atfw::testing::invoke_ss_action<task_action_commit_participator>(ctx, ack_pa, ack_options));
    CASE_EXPECT_EQ(0, res);

    table_type intermediate_record;
    uint64_t intermediate_version = 0;
    CASE_EXPECT_EQ(
        0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "act-create-1", intermediate_record, intermediate_version)));

    atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq ack_pb;
    ack_pb.mutable_metadata()->set_transaction_uuid("act-create-1");
    ack_pb.set_participator_key("pb");
    res = RPC_AWAIT_CODE_RESULT(
        atfw::testing::invoke_ss_action<task_action_commit_participator>(ctx, ack_pb, ack_options));
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
    res =
        RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_reject>(ctx, reject_request, reject_options));
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
    res =
        RPC_AWAIT_CODE_RESULT(atfw::testing::invoke_ss_action<task_action_remove>(ctx, remove_request, remove_options));
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
    CASE_EXPECT_EQ(
        0, test.transport().inject_inbound(
               sender, static_cast<int32_t>(::atfw::component::message_type::kInServerMessage),
               gsl::span<const unsigned char>{reinterpret_cast<const unsigned char*>(payload.data()), payload.size()},
               sequence));
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&test, sequence]() {
      for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
        const atfw::testing::outbound_message* record = test.transport().outbound_at(i);
        if (record != nullptr && record->target_node_id == 0x1300F1 && record->sequence == sequence) {
          return true;
        }
      }
      return false;
    }));
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
    CASE_EXPECT_TRUE(
        response_msg.ParseFromArray(error_response->payload.data(), static_cast<int>(error_response->payload.size())));
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

// ============ 5.1.7 reverse race: reject first, then commit keeps REJECTED ============

CASE_TEST(component_dtcoordsvr, manager_reverse_reject_then_commit) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task = test.run_task("reverse_race", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "mgr-reverse-1", {"pa"});
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));

    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-reverse-1");
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, trans)));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   trans->data_object.metadata().status());

    // The late commit cannot flip the persisted REJECTED terminal.
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   trans->data_object.metadata().status());

    table_type record;
    uint64_t version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-reverse-1", record, version)));
    transaction_blob_storage reloaded;
    CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&reloaded));
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   reloaded.metadata().status());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-018 extension: repeated acks and acks after the global terminal ============

CASE_TEST(component_dtcoordsvr, participator_ack_repeated_and_after_global_terminal_dt018) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task =
      test.run_task("ack_repeated_and_late", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-ack-late-1", {"pa", "pb"});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-ack-late-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));

        // First ack stores pa COMMITED.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pa")));
        table_type record_after_first;
        uint64_t version_after_first = 0;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-ack-late-1", record_after_first, version_after_first)));

        // The repeated ack is an idempotent success and does not write again.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pa")));
        table_type record_after_repeat;
        uint64_t version_after_repeat = 0;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-ack-late-1", record_after_repeat, version_after_repeat)));
        CASE_EXPECT_EQ(version_after_first, version_after_repeat);

        // Global commit after a partial participant ack.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                       trans->data_object.metadata().status());

        // A reject ack arriving after the global terminal may still advance pb's own (non-terminal)
        // status, but the global status stays COMMITED and the mixed-direction record is kept.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_reject(ctx, trans, "pb")));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                       (*trans->data_object.mutable_participators())["pb"].participator_status());
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                       trans->data_object.metadata().status());

        table_type kept_record;
        uint64_t kept_version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-ack-late-1", kept_record, kept_version)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ mutable: a wrong blob type unpacks to an error (4.4 mutable row) ============

CASE_TEST(component_dtcoordsvr, mutable_transaction_bad_any_unpack) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task = test.run_task("bad_any", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    // Seed a record whose blob is a valid protobuf message of the wrong type.
    rpc::shared_message<table_type> db_data{ctx};
    db_data->set_zone_id(kTestZoneId);
    db_data->set_transaction_uuid("mgr-bad-any-1");
    atfw::distributed_system::config::dtcoordsvr_cfg wrong_payload;
    CASE_EXPECT_TRUE(db_data->mutable_blob_data()->PackFrom(wrong_payload));
    uint64_t version = 0;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, db_data, version)));

    transaction_metadata metadata;
    metadata.set_transaction_uuid("mgr-bad-any-1");
    transaction_manager::transaction_ptr_type trans;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK,
                   RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
    CASE_EXPECT_FALSE(!!trans);
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 4.4 tick: duration-based eviction reloads from the DB ============

CASE_TEST(component_dtcoordsvr, manager_tick_evicts_by_duration) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  // The LRU is a process-lifetime singleton: drop leftovers from earlier cases so this case only
  // observes its own entry and capacity eviction cannot satisfy the assertions.
  transaction_manager::me()->clear_lru_for_unit_test();

  auto task =
      test.run_task("duration_eviction_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        // The transaction expire window must outlive the eviction point: mutable_transaction
        // auto-rejects expired records (DT-019), which must not mix into this eviction path.
        dt_test::make_prepared_storage(storage, "mgr-duration-1", {"pa"}, false, std::chrono::seconds{3600});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-duration-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, transaction_manager::me()->get_lru_size_for_unit_test());

  // Advance the business clock past the effective lru_expired_duration (the annotated default 60s;
  // see setup_dtcoordsvr_config_loader). Runtime hard timeouts stay on the raw system clock, so
  // eviction does not depend on scheduler delay.
  {
    dt_test::global_now_offset_guard business_clock(std::chrono::seconds{61});
    int evicted = transaction_manager::me()->tick();
    CASE_EXPECT_EQ(1, evicted);
    CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
  }

  // The reload must actually hit the DB: a cache hit would satisfy the content assertions without
  // proving the eviction. Count get_all through the engine.
  size_t db_calls_before_reload = test.db().calls("distribute_transaction");
  auto reload_task = test.run_task(
      "duration_eviction_reload", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-duration-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        CASE_EXPECT_EQ("mgr-duration-1", trans->data_object.metadata().transaction_uuid());
        CASE_EXPECT_EQ(1, trans->data_version);  // reloaded from the DB, same CAS version
        RPC_RETURN_CODE(0);
      });
  auto reload_result = test.wait(reload_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(reload_result.task_exited);
  CASE_EXPECT_EQ(0, reload_result.result_code);
  CASE_EXPECT_EQ(db_calls_before_reload + 1, test.db().calls("distribute_transaction"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ raw dispatcher: malformed body and wrong type URL are rejected ============

CASE_TEST(component_dtcoordsvr, actions_raw_dispatcher_malformed_payloads) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto sender = test.discovery().add_node(dt_test::make_coordinator_node(0x1300F2, 1));
  CASE_EXPECT_TRUE(!!sender);
  if (!sender) {
    test.stop();
    return;
  }

  auto inject_raw = [&test, &sender](gsl::string_view rpc_name, gsl::string_view type_url, const std::string& body,
                                     uint64_t sequence) {
    atframework::SSMsg msg;
    auto* head = msg.mutable_head();
    head->set_node_id(0x1300F2);
    head->set_node_name("unit-test-raw-malformed");
    head->set_source_task_id(89);
    head->set_sequence(sequence);
    auto* rpc_request = head->mutable_rpc_request();
    rpc_request->set_rpc_name(rpc_name.data(), rpc_name.size());
    rpc_request->set_type_url(type_url.data(), type_url.size());
    msg.mutable_body_bin()->assign(body);
    std::string payload;
    CASE_EXPECT_TRUE(msg.SerializeToString(&payload));
    CASE_EXPECT_EQ(
        0, test.transport().inject_inbound(
               sender, static_cast<int32_t>(::atfw::component::message_type::kInServerMessage),
               gsl::span<const unsigned char>{reinterpret_cast<const unsigned char*>(payload.data()), payload.size()},
               sequence));
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&test, sequence]() {
      for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
        const atfw::testing::outbound_message* record = test.transport().outbound_at(i);
        if (record != nullptr && record->target_node_id == 0x1300F2 && record->sequence == sequence) {
          return true;
        }
      }
      return false;
    }));
  };

  // Unparsable body bytes under the correct type URL.
  inject_raw(rpc::transaction::packer::get_full_name_of_query(),
             atfw::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name(),
             std::string{"\xff\xff\xff\xff"}, 2101);
  // A well-formed body advertised with the wrong type URL.
  atfw::distributed_system::SSDistributeTransactionQueryReq valid_body;
  valid_body.mutable_metadata()->set_transaction_uuid("raw-wrong-type");
  inject_raw(rpc::transaction::packer::get_full_name_of_query(),
             atfw::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name(),
             valid_body.SerializeAsString(), 2102);

  for (uint64_t sequence = 2101; sequence <= 2102; ++sequence) {
    const atfw::testing::outbound_message* response_record = nullptr;
    for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
      const atfw::testing::outbound_message* record = test.transport().outbound_at(i);
      if (record != nullptr && record->target_node_id == 0x1300F2 && record->sequence == sequence) {
        response_record = record;
      }
    }
    CASE_EXPECT_TRUE(response_record != nullptr);
    if (response_record != nullptr) {
      atframework::SSMsg response_msg;
      CASE_EXPECT_TRUE(response_msg.ParseFromArray(response_record->payload.data(),
                                                   static_cast<int>(response_record->payload.size())));
      CASE_EXPECT_NE(0, response_msg.head().error_code());
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ section 3.1 seam: LRU size read + clear between cases ============

CASE_TEST(component_dtcoordsvr, lru_unit_test_seam) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto task = test.run_task("lru_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "mgr-seam-1", {"pa"});
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_GE(transaction_manager::me()->get_lru_size_for_unit_test(), 1);

  // clear_lru_for_unit_test erases every entry from the pool (old handles are still marked removed
  // and can no longer write back through save); a later fetch creates a fresh cache object and
  // re-reads the DB instead of reviving the removed entry.
  transaction_manager::me()->clear_lru_for_unit_test();
  CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());

  auto reload_task =
      test.run_task("lru_reload_after_clear", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-seam-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        CASE_EXPECT_EQ("mgr-seam-1", trans->data_object.metadata().transaction_uuid());
        CASE_EXPECT_GE(transaction_manager::me()->get_lru_size_for_unit_test(), 1);
        RPC_RETURN_CODE(0);
      });
  auto reload_result = test.wait(reload_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(reload_result.task_exited);
  CASE_EXPECT_EQ(0, reload_result.result_code);

  // save(null) is rejected instead of dereferencing the handle (4.4 save row).
  auto guard_task =
      test.run_task("save_null_guard", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_manager::transaction_ptr_type null_trans;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->save(ctx, null_trans)));
        RPC_RETURN_CODE(0);
      });
  auto guard_result = test.wait(guard_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(guard_result.task_exited);
  CASE_EXPECT_EQ(0, guard_result.result_code);

  // Leave the singleton clean for later cases in this executable.
  transaction_manager::me()->clear_lru_for_unit_test();
  CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 4.4: participant ack drains in-flight IO before deleting ============

CASE_TEST(component_dtcoordsvr, participant_ack_drains_inflight_io_before_delete) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  // The first get_all waits behind an explicit gate so the LRU fetch is still in flight when the
  // concurrent reader arrives.
  bool slow_get_consumed = false;
  bool release_slow_get = false;
  auto slow_get_rule = rpc::db::distribute_transaction::mock::get_all(
      [&slow_get_consumed, &release_slow_get](rpc::context& subctx, const table_type& input, table_type& output,
                                              rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
        if (!slow_get_consumed) {
          slow_get_consumed = true;
          for (int i = 0; i < 5000 && !release_slow_get; ++i) {
            RPC_AWAIT_CODE_RESULT(rpc::wait(subctx, std::chrono::milliseconds{1}));
          }
          if (!release_slow_get) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
          }
        }
        output.set_zone_id(input.zone_id());
        output.set_transaction_uuid(input.transaction_uuid());
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-drain-1", {"pa", "pb"});
        ATFW_EXPLICIT_UNUSED_ATTR bool packed = output.mutable_blob_data()->PackFrom(storage);
        meta.version = 1;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!slow_get_rule);

  // Task A starts mutable_transaction and parks inside the slow fetch.
  auto fetch_task =
      test.run_task("drain_fetch", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-drain-1");
        transaction_manager::transaction_ptr_type trans;
        int32_t res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&slow_get_consumed]() { return slow_get_consumed; }));

  // Task B acks both participants; the record is only deleted after the in-flight fetch finished.
  bool ack_started = false;
  bool release_ack = false;
  auto ack_task = test.run_task(
      "drain_ack", std::chrono::seconds{10}, [&ack_started, &release_ack](rpc::context& ctx) -> rpc::result_code_type {
        ack_started = true;
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-drain-1");
        transaction_manager::transaction_ptr_type trans;
        int32_t res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!trans);
        // Let the first reader observe the fetched record before ACK can remove it. Otherwise that reader
        // may correctly return EN_SYS_NOTFOUND when resumed after the last ACK deletes the cache entry.
        for (int i = 0; i < 5000 && !release_ack; ++i) {
          RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
        }
        if (!release_ack) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
        }
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pa")));
        res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pb"));
        CASE_EXPECT_EQ(0, res);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&ack_started]() { return ack_started; }));
  CASE_EXPECT_EQ(0u, test.db().calls("distribute_transaction", atfw::testing::mock_db::op_type::remove_all));
  release_slow_get = true;
  auto fetch_result = test.wait(fetch_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(fetch_result.task_exited);
  CASE_EXPECT_EQ(0, fetch_result.result_code);
  release_ack = true;
  auto ack_result = test.wait(ack_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(ack_result.task_exited);
  CASE_EXPECT_EQ(0, ack_result.result_code);

  // The typed get_all mock must be off before the verification read below.
  slow_get_rule.reset();

  auto check_task =
      test.run_task("drain_check", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        table_type gone_record;
        uint64_t gone_version = 0;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-drain-1", gone_record, gone_version)));
        RPC_RETURN_CODE(0);
      });
  auto check_result = test.wait(check_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(check_result.task_exited);
  CASE_EXPECT_EQ(0, check_result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 4.4: stream remove answers without a response message ============

CASE_TEST(component_dtcoordsvr, actions_raw_dispatcher_stream_remove_no_response) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();

  auto sender = test.discovery().add_node(dt_test::make_coordinator_node(0x1300F3, 1));
  CASE_EXPECT_TRUE(!!sender);
  if (!sender) {
    test.stop();
    return;
  }

  // Seed one record so the remove action has real work to do.
  auto seed_task =
      test.run_task("stream_remove_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-stream-remove-1", {"pa"});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        RPC_RETURN_CODE(0);
      });
  auto seed_result = test.wait(seed_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(seed_result.task_exited);
  CASE_EXPECT_EQ(0, seed_result.result_code);

  // Inject a one-way stream envelope (rpc_stream instead of rpc_request): the remove action must
  // run and delete the record but never answer the sender.
  atframework::SSMsg msg;
  auto* head = msg.mutable_head();
  head->set_node_id(0x1300F3);
  head->set_node_name("unit-test-raw-stream");
  head->set_source_task_id(90);
  head->set_sequence(2201);
  auto* rpc_stream = head->mutable_rpc_stream();
  rpc_stream->set_rpc_name(rpc::transaction::packer::get_full_name_of_remove());
  rpc_stream->set_type_url(atfw::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name());
  atfw::distributed_system::SSDistributeTransactionRemoveReq remove_request;
  remove_request.mutable_metadata()->set_transaction_uuid("mgr-stream-remove-1");
  CASE_EXPECT_TRUE(remove_request.SerializeToString(msg.mutable_body_bin()));
  std::string payload;
  CASE_EXPECT_TRUE(msg.SerializeToString(&payload));
  using db_op_type = rpc::db::hash_table::unit_test_request::op_type;
  const size_t remove_calls_before = test.db().calls("distribute_transaction", db_op_type::remove_all);
  CASE_EXPECT_EQ(
      0, test.transport().inject_inbound(
             sender, static_cast<int32_t>(::atfw::component::message_type::kInServerMessage),
             gsl::span<const unsigned char>{reinterpret_cast<const unsigned char*>(payload.data()), payload.size()},
             2201));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&test, remove_calls_before]() {
    return test.db().calls("distribute_transaction", db_op_type::remove_all) > remove_calls_before;
  }));

  // The record is gone and no outbound response carries the stream sequence.
  auto check_task =
      test.run_task("stream_remove_check", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        table_type gone_record;
        uint64_t gone_version = 0;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-stream-remove-1", gone_record, gone_version)));
        RPC_RETURN_CODE(0);
      });
  auto check_result = test.wait(check_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(check_result.task_exited);
  CASE_EXPECT_EQ(0, check_result.result_code);

  bool has_response = false;
  for (size_t i = 0; i < test.transport().outbound_count(); ++i) {
    const atfw::testing::outbound_message* record = test.transport().outbound_at(i);
    if (record != nullptr && record->target_node_id == 0x1300F3 && record->sequence == 2201) {
      has_response = true;
    }
  }
  CASE_EXPECT_FALSE(has_response);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ §4.4 G：在途 IO 保留缓存入口，完成后恢复容量淘汰 ============
CASE_TEST(component_dtcoordsvr, tick_retains_inflight_fetch_then_resumes_eviction) {
  // 容量淘汰断言需要小容量：注入 lru_max_cache_count=2（注解默认 200000 不会淘汰）
  scoped_dtcoordsvr_capacity capacity_override{2};
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  // 协调者 manager 是进程级单例，LRU 跨用例存活：显式清空以保证用例间独立
  transaction_manager::me()->clear_lru_for_unit_test();

  // 首个 get_all 挂起直至测试放行：mutable_transaction 的 LRU 占位条目在 IO 在途期间已建立
  coordinator_io_gate fetch_gate;
  int get_calls = 0;
  auto gated_get_rule = rpc::db::distribute_transaction::mock::get_all(
      [&fetch_gate, &get_calls](rpc::context& subctx, const table_type& input, table_type& output,
                                rpc::unit_test::db_mock_meta& meta) -> rpc::result_code_type {
        ++get_calls;
        if (1 == get_calls) {
          int result = RPC_AWAIT_CODE_RESULT(fetch_gate.wait(subctx));
          if (result < 0) {
            RPC_RETURN_CODE(result);
          }
        }
        output.set_zone_id(input.zone_id());
        output.set_transaction_uuid(input.transaction_uuid());
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-inflight-1", {"pa"});
        ATFW_EXPLICIT_UNUSED_ATTR bool packed = output.mutable_blob_data()->PackFrom(storage);
        meta.version = 1;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!gated_get_rule);

  auto fetch_task =
      test.run_task("inflight_fetch", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-inflight-1");
        transaction_manager::transaction_ptr_type trans;
        int32_t res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!trans);
        CASE_EXPECT_EQ(1, trans->data_version);
        RPC_RETURN_CODE(0);
      });

  // fetch 在途：占位条目存在于 LRU
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&fetch_gate]() { return fetch_gate.entered(); }));
  CASE_EXPECT_EQ(1, transaction_manager::me()->get_lru_size_for_unit_test());

  // 容量为 2；在途读取占用一个入口，tick 只能淘汰已经完成 IO 的记录。
  auto seed_task =
      test.run_task("inflight_capacity_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        for (const char* uuid : {"mgr-inflight-c1", "mgr-inflight-c2", "mgr-inflight-c3"}) {
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, uuid, {"pa"});
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        }
        RPC_RETURN_CODE(0);
      });
  auto seed_result = test.wait(seed_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(seed_result.task_exited);
  CASE_EXPECT_EQ(0, seed_result.result_code);
  CASE_EXPECT_EQ(4, transaction_manager::me()->get_lru_size_for_unit_test());

  // 跳过在途读取，淘汰 c1 和 c2。
  int evicted = transaction_manager::me()->tick();
  CASE_EXPECT_EQ(2, evicted);
  CASE_EXPECT_EQ(2, transaction_manager::me()->get_lru_size_for_unit_test());

  // 放行 IO 后仍能命中同一缓存，不重新读取数据库。
  CASE_EXPECT_EQ(0, fetch_gate.release());
  auto fetch_result = test.wait(fetch_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(fetch_result.task_exited);
  CASE_EXPECT_EQ(0, fetch_result.result_code);
  CASE_EXPECT_EQ(2, transaction_manager::me()->get_lru_size_for_unit_test());

  // 后续读取共享已完成的缓存。
  auto refetch_task =
      test.run_task("inflight_refetch", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-inflight-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto refetch_result = test.wait(refetch_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(refetch_result.task_exited);
  CASE_EXPECT_EQ(0, refetch_result.result_code);
  CASE_EXPECT_EQ(1, get_calls);

  auto cache_hit_task =
      test.run_task("inflight_cache_hit", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-inflight-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto cache_hit_result = test.wait(cache_hit_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(cache_hit_result.task_exited);
  CASE_EXPECT_EQ(0, cache_hit_result.result_code);
  CASE_EXPECT_EQ(1, get_calls);

  auto evict_completed = test.run_task(
      "inflight_completed_eviction", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        for (const char* uuid : {"mgr-inflight-new1", "mgr-inflight-new2"}) {
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, uuid, {"pa"}, true);
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        }
        CASE_EXPECT_EQ(2, transaction_manager::me()->tick());
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-inflight-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto evicted_completed = test.wait(evict_completed, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(evicted_completed.task_exited);
  CASE_EXPECT_EQ(0, evicted_completed.result_code);
  CASE_EXPECT_EQ(2, get_calls);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ create 阶段 TTL 失败清理记录；save 阶段 TTL 失败保留已保存的决议 ============
CASE_TEST(component_dtcoordsvr, ttl_failure_create_rolls_back_and_save_tolerates) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  // 协调者 manager 是进程级单例，LRU 跨用例存活：显式清空以保证用例间独立
  transaction_manager::me()->clear_lru_for_unit_test();

  bool ttl_should_fail = false;
  int set_ttl_calls = 0;
  auto ttl_rule = rpc::db::distribute_transaction::mock::set_ttl(
      [&ttl_should_fail, &set_ttl_calls](rpc::context&, const table_type&, uint64_t,
                                         rpc::unit_test::db_mock_meta&) -> rpc::result_code_type {
        ++set_ttl_calls;
        RPC_RETURN_CODE(ttl_should_fail ? PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT : 0);
      });
  CASE_EXPECT_TRUE(!!ttl_rule);

  // --- Phase I：create 阶段 TTL 失败 → 回滚：DB 记录删除、不入缓存、错误码透传 ---
  ttl_should_fail = true;
  auto create_task =
      test.run_task("ttl_fail_create", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-ttl-create", {"pa"});
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        // 回滚：DB 记录已删除
        table_type gone_record;
        uint64_t gone_version = 0;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-ttl-create", gone_record, gone_version)));
        RPC_RETURN_CODE(0);
      });
  auto create_result = test.wait(create_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(create_result.task_exited);
  CASE_EXPECT_EQ(0, create_result.result_code);
  CASE_EXPECT_EQ(1, set_ttl_calls);
  CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());

  // --- Phase H：save 阶段 TTL 失败被容忍：replace 已持久化，commit 返回成功且缓存保留 ---
  ttl_should_fail = false;
  auto commit_task = test.run_task(
      "ttl_fail_save", std::chrono::seconds{10}, [&ttl_should_fail](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-ttl-save", {"pa"});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-ttl-save");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);

        // save（commit 写回）阶段 TTL 刷新失败：仅记录日志，结果仍成功
        ttl_should_fail = true;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans)));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                       trans->data_object.metadata().status());

        // 数据已持久化：版本推进到 2，状态为 COMMITED
        table_type record;
        uint64_t version = 0;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-ttl-save", record, version)));
        CASE_EXPECT_EQ(2, version);
        transaction_blob_storage reloaded;
        CASE_EXPECT_TRUE(record.blob_data().UnpackTo(&reloaded));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                       reloaded.metadata().status());
        RPC_RETURN_CODE(0);
      });
  auto commit_result = test.wait(commit_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(commit_result.task_exited);
  CASE_EXPECT_EQ(0, commit_result.result_code);
  CASE_EXPECT_EQ(3, set_ttl_calls);  // create 两次 + save 一次

  // save 成功（replace OK）：缓存条目保留，后续 fetch 命中缓存不再读库
  CASE_EXPECT_EQ(1, transaction_manager::me()->get_lru_size_for_unit_test());
  const size_t db_reads_after_commit = test.db().calls("distribute_transaction", op_type::kv_get_all);
  auto cache_hit_task =
      test.run_task("ttl_fail_cache_hit", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-ttl-save");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        RPC_RETURN_CODE(0);
      });
  auto cache_hit_result = test.wait(cache_hit_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(cache_hit_result.task_exited);
  CASE_EXPECT_EQ(0, cache_hit_result.result_code);
  CASE_EXPECT_EQ(db_reads_after_commit, test.db().calls("distribute_transaction", op_type::kv_get_all));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 参与者 ACK 删除：DB 删除失败保留缓存，重试命中缓存不拉库；成功后旧句柄不可复活 ============
CASE_TEST(component_dtcoordsvr, participant_ack_remove_failure_retains_cache_for_retry) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  auto seed_task =
      test.run_task("remove_retry_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        dt_test::make_prepared_storage(storage, "mgr-remove-retry-1", {"pa", "pb"}, false, std::chrono::seconds{3600});
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        RPC_RETURN_CODE(0);
      });
  auto seed_result = test.wait(seed_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(seed_result.task_exited);
  CASE_EXPECT_EQ(0, seed_result.result_code);

  // 首次 remove_all 报错（模拟 DB 抖动），后续调用落回内存后端真实删除
  int remove_calls = 0;
  auto remove_rule = test.db().mock_table("distribute_transaction");
  remove_rule.on(op_type::remove_all, [&remove_calls](atfw::testing::db_table_context& ctx) -> bool {
    if (++remove_calls == 1) {
      ctx.return_code = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
      return true;  // handled: 本次按报错处理，不落库
    }
    return false;  // fall through: 后端真实删除
  });
  CASE_EXPECT_TRUE(!!remove_rule);
  const size_t get_all_calls_before_ack = test.db().calls("distribute_transaction", op_type::kv_get_all);

  // 第一次 ACK：最后一个参与者触发 all_resolved 删除，remove_all 失败必须保留缓存条目
  transaction_manager::transaction_ptr_type stale_handle;
  auto ack_task = test.run_task(
      "remove_retry_ack", std::chrono::seconds{10}, [&stale_handle](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-remove-retry-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        CASE_EXPECT_TRUE(!!trans);
        stale_handle = trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pa")));
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pb")));
      });
  auto ack_result = test.wait(ack_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(ack_result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, ack_result.result_code);
  CASE_EXPECT_EQ(1, remove_calls);
  // 删除失败：缓存条目保留，供重试与后续请求直接命中（不产生数据库拉取）
  CASE_EXPECT_EQ(1, transaction_manager::me()->get_lru_size_for_unit_test());
  CASE_EXPECT_EQ(get_all_calls_before_ack, test.db().calls("distribute_transaction", op_type::kv_get_all));

  // 参与者重复 ACK：mutable_transaction 命中同一缓存对象（零 get_all），重试删除成功后条目才被移除
  auto retry_task = test.run_task(
      "remove_retry_ack_again", std::chrono::seconds{10}, [&stale_handle](rpc::context& ctx) -> rpc::result_code_type {
        transaction_metadata metadata;
        metadata.set_transaction_uuid("mgr-remove-retry-1");
        transaction_manager::transaction_ptr_type trans;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, trans)));
        if (CASE_EXPECT_TRUE(!!trans)) {
          CASE_EXPECT_EQ(stale_handle.get(), trans.get());
        }
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transaction_manager::me()->try_commit(ctx, trans, "pb")));
      });
  auto retry_result = test.wait(retry_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(retry_result.task_exited);
  CASE_EXPECT_EQ(0, retry_result.result_code);
  CASE_EXPECT_EQ(2, remove_calls);
  CASE_EXPECT_EQ(0, transaction_manager::me()->get_lru_size_for_unit_test());
  CASE_EXPECT_EQ(get_all_calls_before_ack, test.db().calls("distribute_transaction", op_type::kv_get_all));
  remove_rule.reset();

  // 删除成功后旧句柄已置 removed：迟到保存被拒绝，记录不会被复活
  auto verify_task = test.run_task(
      "remove_retry_verify", std::chrono::seconds{10}, [&stale_handle](rpc::context& ctx) -> rpc::result_code_type {
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                       RPC_AWAIT_CODE_RESULT(transaction_manager::me()->save(ctx, stale_handle)));
        stale_handle.reset();
        table_type gone_record;
        uint64_t gone_version = 0;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND,
                       RPC_AWAIT_CODE_RESULT(db_read_record(ctx, "mgr-remove-retry-1", gone_record, gone_version)));
        RPC_RETURN_CODE(0);
      });
  auto verify_result = test.wait(verify_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(verify_result.task_exited);
  CASE_EXPECT_EQ(0, verify_result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ lru_max_cache_count=0 表示不限容量：tick 不做容量淘汰 ============
CASE_TEST(component_dtcoordsvr, tick_capacity_zero_means_unlimited) {
  // 显式注入 0：代码层将 0 解释为不限容量（仅按 lru_expired_duration 过期淘汰）
  scoped_dtcoordsvr_capacity capacity_override{0};
  atfw::testing::runtime test;
  atfw::testing::runtime_options options = make_dtcoordsvr_runtime_options();
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.db().register_message_type<table_type>();
  transaction_manager::me()->clear_lru_for_unit_test();

  auto seed_task =
      test.run_task("capacity_zero_seed", std::chrono::seconds{10}, [](rpc::context& ctx) -> rpc::result_code_type {
        for (const auto* uuid : {"mgr-capacity-0-a", "mgr-capacity-0-b", "mgr-capacity-0-c"}) {
          transaction_blob_storage storage;
          dt_test::make_prepared_storage(storage, uuid, {"pa"}, false, std::chrono::seconds{3600});
          CASE_EXPECT_EQ(0,
                         RPC_AWAIT_CODE_RESULT(transaction_manager::me()->create_transaction(ctx, std::move(storage))));
        }
        RPC_RETURN_CODE(0);
      });
  auto seed_result = test.wait(seed_task, std::chrono::seconds{20});
  CASE_EXPECT_TRUE(seed_result.task_exited);
  CASE_EXPECT_EQ(0, seed_result.result_code);
  CASE_EXPECT_EQ(3, transaction_manager::me()->get_lru_size_for_unit_test());

  // 超出"容量 0"的条目不做容量淘汰；只有超过 lru_expired_duration 的条目才过期
  int evicted = transaction_manager::me()->tick();
  CASE_EXPECT_EQ(0, evicted);
  CASE_EXPECT_EQ(3, transaction_manager::me()->get_lru_size_for_unit_test());

  CASE_EXPECT_EQ(0, test.stop());
}
