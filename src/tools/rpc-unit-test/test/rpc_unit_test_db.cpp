// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rpc_unit_test.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "frame/test_macros.h"
#include "rpc/db/hash_table.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/db/uuid.h"
#include "rpc/unit_test/rpcunittestservice.atfw.gen.h"

namespace {
constexpr uint32_t kTestDbChannel = static_cast<uint32_t>(db_msg_dispatcher::channel_t::RAW_DEFAULT);

using op_type = atfw::testing::mock_db::op_type;

bool start_db_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::db};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  test.db().register_message_type<rpc_unit_test::RpcUnitTestTable>();
  test.db().register_message_type<rpc_unit_test::RpcUnitTestListEntry>();
  return true;
}

atfw::testing::mock_node make_db_remote_node(uint64_t id, const char *name) {
  atfw::testing::mock_node node;
  node.set_id(id).set_name(name).set_type_id(4097).set_type_name("rpc-unit-test-remote").set_zone_id(1);
  return node;
}
}  // namespace

CASE_TEST(rpc_unit_test, db_kv_set_get_all_and_cas_version) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_kv_cas", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    rpc::shared_message<rpc_unit_test::RpcUnitTestTable> table{ctx};
    table->set_name("alice");
    table->set_counter(7);

    uint64_t version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:cas", rpc::shared_abstract_message<google::protobuf::Message>{table}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:cas", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output->version));
    if (output->message) {
      const auto &restored = static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get());
      CASE_EXPECT_TRUE(restored.has_name());
      CASE_EXPECT_EQ("alice", restored.name());
      CASE_EXPECT_EQ(7, static_cast<int>(restored.counter()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // Matching CAS: applied and the version advances.
    table->set_counter(8);
    uint64_t matched_version = 1;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:cas", rpc::shared_abstract_message<google::protobuf::Message>{table},
        &matched_version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(matched_version));

    // Stale CAS (expected 1 while 2 is stored): rejected and the stored version is reported back.
    // An expected version of 0 is never stale - it forces the overwrite, see below.
    table->set_counter(9999);
    uint64_t stale_version = 1;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:cas", rpc::shared_abstract_message<google::protobuf::Message>{table},
        &stale_version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
    CASE_EXPECT_EQ(2, static_cast<int>(stale_version));

    // The conflicting write did not land.
    table->set_counter(8);
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:cas", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(
          8, static_cast<int>(static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get()).counter()));
    }

    // Expected version 0 ignores the CAS check entirely (except_version == 0 branch of the script):
    // the versioned record is overwritten regardless of its stored version, and the stored version
    // still bumps from the real one (2 -> 3), never from the expected one.
    table->set_counter(9);
    uint64_t forced_version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:cas", rpc::shared_abstract_message<google::protobuf::Message>{table},
        &forced_version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(forced_version));

    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:cas", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(
          9, static_cast<int>(static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get()).counter()));
    }

    // A versionless record (only written by unversioned HSET) accepts any expected version and
    // starts its CAS sequence at 1, matching the embedded Lua script.
    table->set_name("cas2");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:cas2", rpc::shared_abstract_message<google::protobuf::Message>{table}, nullptr));
    CASE_EXPECT_EQ(0, res);
    uint64_t any_version = 5;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:cas2", rpc::shared_abstract_message<google::protobuf::Message>{table},
        &any_version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(any_version));
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(6, static_cast<int>(test.db().calls(op_type::kv_set)));
  CASE_EXPECT_EQ(3, static_cast<int>(test.db().calls(op_type::kv_get_all)));
  CASE_EXPECT_EQ(3, static_cast<int>(test.db().get_version("ut:kv:cas")));
  CASE_EXPECT_EQ(0, test.stop());
}

// kInsertHashTable contract: only a key without a stored CAS version can be inserted; a duplicate
// fails with EN_DB_KEY_EXISTS (the caller's mapping of the script's CAS_FAILED reply) with the
// stored version reported back; a record written only by the unversioned set still counts as
// absent for the script and accepts the insert.
CASE_TEST(rpc_unit_test, db_kv_insert_only_contract) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_kv_insert", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    rpc::shared_message<rpc_unit_test::RpcUnitTestTable> table{ctx};
    table->set_name("insert-only");
    table->set_counter(11);

    uint64_t version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
        ctx, kTestDbChannel, "ut:kv:insert", rpc::shared_abstract_message<google::protobuf::Message>{table}, version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    // The record now carries a CAS version: rejected without modifying the stored data.
    table->set_counter(12);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
        ctx, kTestDbChannel, "ut:kv:insert", rpc::shared_abstract_message<google::protobuf::Message>{table}, version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:insert", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(11,
                     static_cast<int>(static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get())
                                          .counter()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // No CAS_VERSION field was ever written by the unversioned HSET: the insert script still reads
    // version 0 and accepts the insert into the existing data.
    table->set_name("insert-unversioned");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:insert-unversioned", rpc::shared_abstract_message<google::protobuf::Message>{table},
        nullptr));
    CASE_EXPECT_EQ(0, res);

    table->set_counter(21);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
        ctx, kTestDbChannel, "ut:kv:insert-unversioned", rpc::shared_abstract_message<google::protobuf::Message>{table},
        version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(3, static_cast<int>(test.db().calls(op_type::kv_insert)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls(op_type::kv_set)));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, db_kv_get_missing_and_partly_get_presence) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task =
      test.run_task("db_kv_missing_partly", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:missing", output, nullptr));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, res);

        // kl get_by_indexs on a missing key behaves like the real HMGET: success with one
        // null-message slot per requested index.
        std::vector<db_key_list_message_result_t> list_output;
        uint64_t missing_indexes[] = {7, 8};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
            ctx, kTestDbChannel, "ut:kl:missing", gsl::span<uint64_t>{missing_indexes}, list_output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(2, static_cast<int>(list_output.size()));
        if (list_output.size() >= 2) {
          CASE_EXPECT_EQ(7, static_cast<int>(list_output[0].list_index));
          CASE_EXPECT_FALSE(!!list_output[0].message);
          CASE_EXPECT_EQ(8, static_cast<int>(list_output[1].list_index));
          CASE_EXPECT_FALSE(!!list_output[1].message);
        }

        rpc::shared_message<rpc_unit_test::RpcUnitTestTable> table{ctx};
        table->set_name("bob");
        table->set_counter(42);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:partly", rpc::shared_abstract_message<google::protobuf::Message>{table},
            nullptr));
        CASE_EXPECT_EQ(0, res);

        // Only counter is requested: name must be cleared even though it is stored.
        gsl::string_view fields[] = {"counter"};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::partly_get(ctx, kTestDbChannel, "ut:kv:partly",
                                                                               fields, 1, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        if (output->message) {
          const auto &restored = static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get());
          CASE_EXPECT_EQ(42, static_cast<int>(restored.counter()));
          CASE_EXPECT_FALSE(restored.has_name());
        } else {
          CASE_EXPECT_TRUE(false);
        }

        // Only name is requested: explicit presence survives the store/parse round trip.
        gsl::string_view name_fields[] = {"name"};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::partly_get(ctx, kTestDbChannel, "ut:kv:partly",
                                                                               name_fields, 1, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        if (output->message) {
          const auto &restored = static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get());
          CASE_EXPECT_TRUE(restored.has_name());
          CASE_EXPECT_EQ("bob", restored.name());
          CASE_EXPECT_EQ(0, static_cast<int>(restored.counter()));
        } else {
          CASE_EXPECT_TRUE(false);
        }
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, db_kv_inc_field_and_uuid_allocator) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_inc_and_uuid", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    // HINCRBY on a missing hash creates it; the new value is written back into the message.
    rpc::shared_message<rpc_unit_test::RpcUnitTestTable> inc_msg{ctx};
    inc_msg->set_counter(5);
    rpc::shared_abstract_message<google::protobuf::Message> inc_wrapper{inc_msg};
    int32_t res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::inc_field(ctx, kTestDbChannel, "ut:kv:inc", "counter", inc_wrapper, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(5, static_cast<int>(inc_msg->counter()));

    inc_msg->set_counter(3);
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::inc_field(ctx, kTestDbChannel, "ut:kv:inc", "counter", inc_wrapper, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(8, static_cast<int>(inc_msg->counter()));

    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:inc", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    if (output->message) {
      CASE_EXPECT_EQ(
          8, static_cast<int>(static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get()).counter()));
    }
    // HINCRBY never touches CAS_VERSION.
    CASE_EXPECT_EQ(0, static_cast<int>(output->version));

    // The uuid allocator flows through the same inc_field path.
    int64_t first_id = RPC_AWAIT_TYPE_RESULT(
        rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 1, 0));
    CASE_EXPECT_TRUE(first_id > 0);
    int64_t second_id = RPC_AWAIT_TYPE_RESULT(
        rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 1, 0));
    CASE_EXPECT_TRUE(second_id > first_id);
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(3, static_cast<int>(test.db().calls(op_type::kv_inc_field)));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, db_key_list_add_get_update_remove) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_key_list", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    rpc::shared_message<rpc_unit_test::RpcUnitTestListEntry> entry{ctx};
    entry->set_id(100);
    entry->set_payload("one");
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, kTestDbChannel, "ut:kl:1", 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    entry->set_id(200);
    entry->set_payload("two");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, kTestDbChannel, "ut:kl:1", 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    std::vector<db_key_list_message_result_t> output;
    res =
        RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:1", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output.size()));
    if (output.size() >= 2 && output[0].message && output[1].message) {
      CASE_EXPECT_EQ(1, static_cast<int>(output[0].list_index));
      CASE_EXPECT_EQ(2, static_cast<int>(output[1].list_index));
      CASE_EXPECT_EQ("one",
                     static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[0].message->get()).payload());
      CASE_EXPECT_EQ("two",
                     static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[1].message->get()).payload());
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // One slot per requested index; missing entries keep a null message without collapsing.
    uint64_t indexes[] = {2, 99};
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
        ctx, kTestDbChannel, "ut:kl:1", gsl::span<uint64_t>{indexes}, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output.size()));
    if (output.size() >= 2) {
      CASE_EXPECT_TRUE(!!output[0].message);
      if (output[0].message) {
        CASE_EXPECT_EQ(
            200,
            static_cast<int>(static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[0].message->get()).id()));
      }
      CASE_EXPECT_EQ(99, static_cast<int>(output[1].list_index));
      CASE_EXPECT_FALSE(!!output[1].message);
    }

    entry->set_id(100);
    entry->set_payload("one-v2");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(
        ctx, kTestDbChannel, "ut:kl:1", 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    uint64_t remove_indexes[] = {2};
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(ctx, kTestDbChannel, "ut:kl:1",
                                                                               gsl::span<uint64_t>{remove_indexes}));
    CASE_EXPECT_EQ(0, res);

    res =
        RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:1", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
    if (!output.empty() && output[0].message) {
      CASE_EXPECT_EQ("one-v2",
                     static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[0].message->get()).payload());
    }

    // max_list_length trims the oldest entries.
    entry->set_payload("three");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, kTestDbChannel, "ut:kl:1", 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);
    res =
        RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:1", output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
    if (!output.empty() && output[0].message) {
      CASE_EXPECT_EQ("three",
                     static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[0].message->get()).payload());
    }
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(3, static_cast<int>(test.db().calls(op_type::kl_add_index)));

  CASE_EXPECT_EQ(0, test.stop());
}

// kAddListIndexHashTable eviction contract: the script counts every hash field including the
// monotonic counter, so an add at capacity (entries >= max_list_length, also for max 0) evicts
// exactly one entry - the smallest index, not the oldest insertion position - before appending the
// next monotonic index.
CASE_TEST(rpc_unit_test, db_kl_add_index_eviction_contract) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task =
      test.run_task("db_kl_eviction", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<rpc_unit_test::RpcUnitTestListEntry> entry{ctx};
        std::vector<db_key_list_message_result_t> output;

        for (int i = 0; i < 3; ++i) {
          entry->set_id(static_cast<uint64_t>(100 + i));
          int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
              ctx, kTestDbChannel, "ut:kl:evict", static_cast<uint64_t>(10),
              rpc::shared_abstract_message<google::protobuf::Message>{entry}));
          CASE_EXPECT_EQ(0, res);
        }

        // Reorder so the smallest index sits at the newest insertion position: remove index 1 and
        // re-create it through update_by_index (which appends the missing index at the back).
        uint64_t removed[] = {1};
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(
            ctx, kTestDbChannel, "ut:kl:evict", gsl::span<uint64_t>{removed}));
        CASE_EXPECT_EQ(0, res);
        entry->set_payload("recreated");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(
            ctx, kTestDbChannel, "ut:kl:evict", 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        // At capacity the add evicts the smallest index (1) and appends a fresh monotonic one (4).
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:evict", 3, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        uint64_t probe[] = {1, 2, 3, 4};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
            ctx, kTestDbChannel, "ut:kl:evict", gsl::span<uint64_t>{probe}, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(4, static_cast<int>(output.size()));
        if (output.size() == 4) {
          CASE_EXPECT_FALSE(!!output[0].message);  // index 1 was evicted
          CASE_EXPECT_TRUE(!!output[1].message);   // index 2 survived
          CASE_EXPECT_TRUE(!!output[2].message);   // index 3 survived
          CASE_EXPECT_TRUE(!!output[3].message);   // index 4 was appended
        }

        // max_list_length 0 still counts the counter field: exactly one entry is kept.
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:evict-max0", 0,
            rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:evict-max0", 0,
            rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:evict-max0", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        if (output.size() == 1) {
          // The evicted index is never reused: the survivor carries the second allocated index.
          CASE_EXPECT_EQ(2, static_cast<int>(output[0].list_index));
        }
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(6, static_cast<int>(test.db().calls(op_type::kl_add_index)));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, db_ttl_expiry_and_remove_all) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  test.db().set_now(std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}});

  auto task = test.run_task("db_ttl", std::chrono::seconds{2}, [&test](rpc::context &ctx) -> rpc::result_code_type {
    rpc::shared_message<rpc_unit_test::RpcUnitTestTable> table{ctx};
    table->set_name("ttl");
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:ttl", rpc::shared_abstract_message<google::protobuf::Message>{table}, nullptr));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set_ttl(ctx, kTestDbChannel, "ut:kv:ttl", 60));
    CASE_EXPECT_EQ(0, res);

    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:ttl", output, nullptr));
    CASE_EXPECT_EQ(0, res);

    // Advance past the TTL: the record expires lazily.
    test.db().advance(std::chrono::seconds{61});
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:ttl", output, nullptr));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, res);

    // remove_ttl pins the record.
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, kTestDbChannel, "ut:kv:ttl", rpc::shared_abstract_message<google::protobuf::Message>{table}, nullptr));
    CASE_EXPECT_EQ(0, res);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set_ttl(ctx, kTestDbChannel, "ut:kv:ttl", 60));
    CASE_EXPECT_EQ(0, res);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::remove_ttl(ctx, kTestDbChannel, "ut:kv:ttl"));
    CASE_EXPECT_EQ(0, res);
    test.db().advance(std::chrono::seconds{120});
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:ttl", output, nullptr));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::remove_all(ctx, kTestDbChannel, "ut:kv:ttl"));
    CASE_EXPECT_EQ(0, res);
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:ttl", output, nullptr));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, res);

    // Production contract: EXPIRE on a missing key is a successful no-op (integer reply 0 mapped
    // through unpack_nothing), not an error.
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set_ttl(ctx, kTestDbChannel, "ut:kv:ttl", 60));
    CASE_EXPECT_EQ(0, res);
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_FALSE(test.db().has_key("ut:kv:ttl"));
  CASE_EXPECT_EQ(3, static_cast<int>(test.db().calls(op_type::set_ttl)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls(op_type::remove_ttl)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls(op_type::remove_all)));

  CASE_EXPECT_EQ(0, test.stop());
}

// A key_list write (add_index/update_by_index) to a lazily-expired key must start a fresh record without
// the stale TTL and without the dead entries (Redis semantics), not revive the expired record.
CASE_TEST(rpc_unit_test, db_key_list_write_after_expiry_starts_fresh) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  test.db().set_now(std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}});

  auto task =
      test.run_task("db_kl_expiry_write", std::chrono::seconds{2}, [&test](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<rpc_unit_test::RpcUnitTestListEntry> entry{ctx};
        entry->set_id(1);
        entry->set_payload("old");
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:ttl", 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set_ttl(ctx, kTestDbChannel, "ut:kl:ttl", 60));
        CASE_EXPECT_EQ(0, res);

        // Past the TTL: the record is logically gone. Appending must create a fresh record holding only the
        // new entry, and the fresh record must not carry the expired TTL.
        test.db().advance(std::chrono::seconds{61});
        entry->set_id(2);
        entry->set_payload("new");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:ttl", 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        std::vector<db_key_list_message_result_t> output;
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:ttl", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        if (!output.empty() && output[0].message) {
          CASE_EXPECT_EQ("new",
                         static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[0].message->get()).payload());
        }

        // The fresh record has no TTL: advancing far past the original deadline must not expire it.
        test.db().advance(std::chrono::seconds{3600});
        output.clear();
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:ttl", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// SS-style typed per-interface mock of the generated per-table API (rpc::db::login_auth::mock): each
// generated table interface accepts one handler (typed input/output messages + CAS version); interfaces
// without a registered handler fall through to the common in-memory backend.
CASE_TEST(rpc_unit_test, db_login_auth_typed_mock_table_interface) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();

  // Arrange through the real write path (served by the in-memory backend).
  auto arrange_task =
      test.run_task("db_login_auth_arrange", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> store{ctx};
        store->set_open_id("openid-smoke");
        store->set_user_id(42);
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, store, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!arrange_task.empty()) {
    auto arrange_result = test.wait(arrange_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(arrange_result.task_exited);
    CASE_EXPECT_EQ(0, arrange_result.result_code);
  }

  // Intercept get_all: the handler sees the typed input (key fields), fills the output record and the
  // CAS version; the backend is bypassed entirely.
  auto get_rule = rpc::db::login_auth::mock::get_all(
      [](rpc::context &, const PROJECT_NAMESPACE_ID::table_login_auth &input,
         PROJECT_NAMESPACE_ID::table_login_auth &output, rpc::unit_test::db_mock_meta &meta) -> rpc::result_code_type {
        CASE_EXPECT_EQ("openid-smoke", input.open_id());
        output.set_open_id(input.open_id());
        output.set_user_id(100);
        meta.version = 9;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!get_rule);
  auto get_task = test.run_task(
      "db_login_auth_mocked_get", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-smoke", *rsp, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(100, static_cast<int>(rsp->user_id()));
        CASE_EXPECT_EQ(9, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!get_task.empty()) {
    auto get_result = test.wait(get_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(get_result.task_exited);
    CASE_EXPECT_EQ(0, get_result.result_code);
  }

  // Intercept replace: version carries the expected CAS version in and the new version out.
  auto replace_rule =
      rpc::db::login_auth::mock::replace([](rpc::context &, const PROJECT_NAMESPACE_ID::table_login_auth &input,
                                            rpc::unit_test::db_mock_meta &meta) -> rpc::result_code_type {
        CASE_EXPECT_EQ("openid-smoke", input.open_id());
        CASE_EXPECT_EQ(43, static_cast<int>(input.user_id()));
        CASE_EXPECT_EQ(1, static_cast<int>(meta.version));
        meta.version = 7;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!replace_rule);
  auto replace_task = test.run_task(
      "db_login_auth_mocked_replace", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> store{ctx};
        store->set_open_id("openid-smoke");
        store->set_user_id(43);
        uint64_t version = 1;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, store, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(7, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!replace_task.empty()) {
    auto replace_result = test.wait(replace_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(replace_result.task_exited);
    CASE_EXPECT_EQ(0, replace_result.result_code);
  }

  // remove_all has no registered handler: it falls through to the in-memory backend.
  auto remove_task = test.run_task(
      "db_login_auth_fallthrough_remove", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::remove_all(ctx, "openid-smoke"));
        CASE_EXPECT_EQ(0, res);
        RPC_RETURN_CODE(0);
      });
  if (!remove_task.empty()) {
    auto remove_result = test.wait(remove_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(remove_result.task_exited);
    CASE_EXPECT_EQ(0, remove_result.result_code);
  }

  // After the handles end, get_all falls through again and the backend record is gone.
  get_rule.reset();
  replace_rule.reset();
  auto restored_task = test.run_task(
      "db_login_auth_restored_get", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-smoke", *rsp, version));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, res);
        RPC_RETURN_CODE(0);
      });
  if (!restored_task.empty()) {
    auto restored_result = test.wait(restored_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(restored_result.task_exited);
    CASE_EXPECT_EQ(0, restored_result.result_code);
  }

  // Intercepted calls never reach the engine hook; only the fallthrough ops are in the history.
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls("login_auth", op_type::kv_set)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls("login_auth", op_type::remove_all)));

  CASE_EXPECT_EQ(0, test.stop());
}

// A DB mock handler is a coroutine (rpc::result_code_type) driven inline at the generated interface
// entry: it may await nested RPC calls (here one SS RPC) before filling output/meta.
CASE_TEST(rpc_unit_test, db_mock_handler_awaits_nested_rpc) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::db, atfw::testing::feature::ss};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();

  auto remote = test.discovery().add_node(make_db_remote_node(0x130071, "unit-test-remote-db-nested"));
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }
  auto ss_rule = test.ss().mock(
      rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user(),
      rpc_unit_test::RpcUnitTestEchoReq::descriptor()->full_name(),
      rpc_unit_test::RpcUnitTestEchoRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view &request, google::protobuf::Message &response) -> rpc::result_code_type {
        const auto &typed_request = static_cast<const rpc_unit_test::RpcUnitTestEchoReq &>(request.body);
        static_cast<rpc_unit_test::RpcUnitTestEchoRsp &>(response).set_echo("nested:" + typed_request.payload());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ss_rule);

  auto db_rule = rpc::db::login_auth::mock::get_all(
      [](rpc::context &ctx, const PROJECT_NAMESPACE_ID::table_login_auth &input,
         PROJECT_NAMESPACE_ID::table_login_auth &output, rpc::unit_test::db_mock_meta &meta) -> rpc::result_code_type {
        // Nested coroutine call: await one SS RPC inside the DB mock handler.
        rpc_unit_test::RpcUnitTestEchoReq nested_req;
        nested_req.set_payload("db-handler");
        rpc_unit_test::RpcUnitTestEchoRsp nested_rsp;
        int32_t res = RPC_AWAIT_CODE_RESULT(
            rpc::unit_test::rpc_unit_test_user(ctx, 0x130071, 1, 10001, "openid-nested", nested_req, nested_rsp));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ("nested:db-handler", nested_rsp.echo());
        output.set_open_id(input.open_id());
        output.set_user_id(81);
        meta.version = 3;
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_TRUE(!!db_rule);

  auto task =
      test.run_task("db_nested_handler", std::chrono::seconds{4}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-nested", *rsp, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(81, static_cast<int>(rsp->user_id()));
        CASE_EXPECT_EQ(3, static_cast<int>(version));
        RPC_RETURN_CODE(res);
      });
  if (task.empty()) {
    test.stop();
    return;
  }
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // The nested SS call went through the SS mock engine (recorded once); the intercepted DB call never
  // reached the DB engine hook.
  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user())));
  CASE_EXPECT_EQ(0, static_cast<int>(test.db().calls("login_auth")));

  CASE_EXPECT_EQ(0, test.stop());
}

// Per-table per-interface callbacks: the handler inspects the request inputs (key, stored record), sets
// the return code and the returned record/version, and operations without a callback fall through to the
// common in-memory backend.
CASE_TEST(rpc_unit_test, db_table_callback_inspects_input_and_sets_output) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();

  // Seed one record through the engine raw entry API (canonical key layout).
  PROJECT_NAMESPACE_ID::table_login_auth seed;
  seed.set_open_id("openid-callback");
  seed.set_user_id(7);
  const std::string seed_key =
      std::string(db_msg_dispatcher::me()->get_record_prefix()) + "-login_auth.openid-callback";
  const std::string seed_type_name(PROJECT_NAMESPACE_ID::table_login_auth::descriptor()->full_name());
  test.db().set_raw_kv(seed_key, seed_type_name, seed.SerializeAsString(), 1);

  auto rule = test.db().mock_table("login_auth");
  CASE_EXPECT_TRUE(!!rule);

  // Spy on writes: inspect the record being stored, then decline so the common backend persists it.
  std::string last_stored_open_id;
  uint64_t last_stored_user_id = 0;
  rule.on(op_type::kv_set, [&](atfw::testing::db_table_context &context) {
    CASE_EXPECT_EQ(op_type::kv_set, context.op);
    CASE_EXPECT_EQ("login_auth", context.table_name);
    if (nullptr != context.input_table) {
      const auto &stored = static_cast<const PROJECT_NAMESPACE_ID::table_login_auth &>(*context.input_table);
      last_stored_open_id = stored.open_id();
      last_stored_user_id = stored.user_id();
    }
    return false;
  });

  // Reads of the untouched key fall through to the common backend.
  auto fallthrough_task =
      test.run_task("db_callback_fallthrough", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-callback", *rsp, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(7, static_cast<int>(rsp->user_id()));
        CASE_EXPECT_EQ(1, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!fallthrough_task.empty()) {
    auto fallthrough_result = test.wait(fallthrough_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(fallthrough_result.task_exited);
    CASE_EXPECT_EQ(0, fallthrough_result.result_code);
  }

  // Write through the generated API: the spy sees the stored record and the common backend persists it
  // (declined callback), bumping the version.
  auto write_task =
      test.run_task("db_callback_write", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 1;
        rsp->set_open_id("openid-callback");
        rsp->set_user_id(8);
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, rsp, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(2, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!write_task.empty()) {
    auto write_result = test.wait(write_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(write_result.task_exited);
    CASE_EXPECT_EQ(0, write_result.result_code);
  }
  CASE_EXPECT_EQ("openid-callback", last_stored_open_id);
  CASE_EXPECT_EQ(8, static_cast<int>(last_stored_user_id));

  // Now intercept reads: serve a canned record + version with a success code, bypassing the backend.
  rule.on(op_type::kv_get_all, [&](atfw::testing::db_table_context &context) {
    CASE_EXPECT_TRUE(context.key.find("openid-callback") != gsl::string_view::npos);
    PROJECT_NAMESPACE_ID::table_login_auth canned;
    canned.set_open_id("openid-canned");
    canned.set_user_id(100);
    CASE_EXPECT_TRUE(test.db().make_output(*context.rpc_context, canned, context.output_table));
    context.output_version = 9;
    context.return_code = 0;
    return true;
  });
  auto canned_task =
      test.run_task("db_callback_canned_read", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-callback", *rsp, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(100, static_cast<int>(rsp->user_id()));
        CASE_EXPECT_EQ(9, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!canned_task.empty()) {
    auto canned_result = test.wait(canned_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(canned_result.task_exited);
    CASE_EXPECT_EQ(0, canned_result.result_code);
  }
  // The backend record is untouched by the handled read.
  CASE_EXPECT_EQ(2, static_cast<int>(test.db().get_version(seed_key)));

  // Table-level catch-all for remaining interfaces: report a custom error for remove_all while kv_set
  // and kv_get_all still have their own callbacks and everything else keeps falling through.
  rule.on_any([](atfw::testing::db_table_context &context) {
    if (op_type::remove_all == context.op) {
      context.return_code = -23456;
      return true;
    }
    return false;
  });
  auto remove_task =
      test.run_task("db_callback_catch_all", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::remove_all(ctx, "openid-callback"));
        CASE_EXPECT_EQ(-23456, res);
        RPC_RETURN_CODE(0);
      });
  if (!remove_task.empty()) {
    auto remove_result = test.wait(remove_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(remove_result.task_exited);
    CASE_EXPECT_EQ(0, remove_result.result_code);
  }

  // Ending the rule restores the common layer for every interface.
  rule.reset();
  auto restored_task =
      test.run_task("db_callback_rule_reset", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> rsp{ctx};
        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-callback", *rsp, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(8, static_cast<int>(rsp->user_id()));
        CASE_EXPECT_EQ(2, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (!restored_task.empty()) {
    auto restored_result = test.wait(restored_task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(restored_result.task_exited);
    CASE_EXPECT_EQ(0, restored_result.result_code);
  }

  CASE_EXPECT_EQ(0, test.stop());
}
