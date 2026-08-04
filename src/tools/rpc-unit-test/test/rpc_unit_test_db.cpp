// Copyright 2026 atframework

#include <chrono>
#include <cstdint>
#include <vector>

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include "frame/test_macros.h"
#include "protocol/pbdesc/rpc_unit_test.pb.h"
#include "rpc/db/hash_table.h"
#include "rpc/db/uuid.h"

namespace {
constexpr uint32_t kTestDbChannel = static_cast<uint32_t>(db_msg_dispatcher::channel_t::RAW_DEFAULT);

using op_type = atsf4g::testing::mock_db::op_type;

bool start_db_runtime(atsf4g::testing::runtime &test) {
  atsf4g::testing::runtime_options options;
  options.features = {atsf4g::testing::feature::db};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  test.db().register_message_type<rpc_unit_test::RpcUnitTestTable>();
  test.db().register_message_type<rpc_unit_test::RpcUnitTestListEntry>();
  return true;
}
}  // namespace

CASE_TEST(rpc_unit_test, db_kv_set_get_all_and_cas_version) {
  atsf4g::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task(
      "db_kv_cas", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<rpc_unit_test::RpcUnitTestTable> table{ctx};
        table->set_name("alice");
        table->set_counter(7);

        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:cas",
            rpc::shared_abstract_message<google::protobuf::Message>{table}, &version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));

        auto output = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
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

        // Stale CAS: rejected and the stored version is reported back.
        table->set_counter(8);
        uint64_t stale_version = 0;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:cas",
            rpc::shared_abstract_message<google::protobuf::Message>{table}, &stale_version));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
        CASE_EXPECT_EQ(1, static_cast<int>(stale_version));

        // Matching CAS: applied and the version advances.
        uint64_t matched_version = 1;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:cas",
            rpc::shared_abstract_message<google::protobuf::Message>{table}, &matched_version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(2, static_cast<int>(matched_version));

        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:cas", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(2, static_cast<int>(output->version));
        if (output->message) {
          CASE_EXPECT_EQ(8, static_cast<int>(
                                static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get())
                                    .counter()));
        }

        // A versionless record (only written by unversioned HSET) accepts any expected version and
        // starts its CAS sequence at 1, matching the embedded Lua script.
        table->set_name("cas2");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:cas2",
            rpc::shared_abstract_message<google::protobuf::Message>{table}, nullptr));
        CASE_EXPECT_EQ(0, res);
        uint64_t any_version = 5;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:cas2",
            rpc::shared_abstract_message<google::protobuf::Message>{table}, &any_version));
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

  CASE_EXPECT_EQ(5, static_cast<int>(test.db().calls(op_type::kv_set)));
  CASE_EXPECT_EQ(2, static_cast<int>(test.db().calls(op_type::kv_get_all)));
  CASE_EXPECT_EQ(2, static_cast<int>(test.db().get_version("ut:kv:cas")));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(rpc_unit_test, db_kv_get_missing_and_partly_get_presence) {
  atsf4g::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task(
      "db_kv_missing_partly", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        auto output = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
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
            ctx, kTestDbChannel, "ut:kv:partly",
            rpc::shared_abstract_message<google::protobuf::Message>{table}, nullptr));
        CASE_EXPECT_EQ(0, res);

        // Only counter is requested: name must be cleared even though it is stored.
        gsl::string_view fields[] = {"counter"};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::partly_get(
            ctx, kTestDbChannel, "ut:kv:partly", fields, 1, output, nullptr));
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
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::partly_get(
            ctx, kTestDbChannel, "ut:kv:partly", name_fields, 1, output, nullptr));
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
  atsf4g::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task(
      "db_inc_and_uuid", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        // HINCRBY on a missing hash creates it; the new value is written back into the message.
        rpc::shared_message<rpc_unit_test::RpcUnitTestTable> inc_msg{ctx};
        inc_msg->set_counter(5);
        rpc::shared_abstract_message<google::protobuf::Message> inc_wrapper{inc_msg};
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::inc_field(
            ctx, kTestDbChannel, "ut:kv:inc", "counter", inc_wrapper, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(5, static_cast<int>(inc_msg->counter()));

        inc_msg->set_counter(3);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::inc_field(
            ctx, kTestDbChannel, "ut:kv:inc", "counter", inc_wrapper, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(8, static_cast<int>(inc_msg->counter()));

        auto output = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_value::get_all(ctx, kTestDbChannel, "ut:kv:inc", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        if (output->message) {
          CASE_EXPECT_EQ(8, static_cast<int>(
                                static_cast<const rpc_unit_test::RpcUnitTestTable &>(*output->message->get())
                                    .counter()));
        }
        // HINCRBY never touches CAS_VERSION.
        CASE_EXPECT_EQ(0, static_cast<int>(output->version));

        // The uuid allocator flows through the same inc_field path.
        int64_t first_id = RPC_AWAIT_TYPE_RESULT(rpc::db::uuid::generate_global_unique_id(
            ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 1, 0));
        CASE_EXPECT_TRUE(first_id > 0);
        int64_t second_id = RPC_AWAIT_TYPE_RESULT(rpc::db::uuid::generate_global_unique_id(
            ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 1, 0));
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
  atsf4g::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task(
      "db_key_list", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<rpc_unit_test::RpcUnitTestListEntry> entry{ctx};
        entry->set_id(100);
        entry->set_payload("one");
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:1", 10,
            rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        entry->set_id(200);
        entry->set_payload("two");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:1", 10,
            rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        std::vector<db_key_list_message_result_t> output;
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:1", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(2, static_cast<int>(output.size()));
        if (output.size() >= 2 && output[0].message && output[1].message) {
          CASE_EXPECT_EQ(1, static_cast<int>(output[0].list_index));
          CASE_EXPECT_EQ(2, static_cast<int>(output[1].list_index));
          CASE_EXPECT_EQ("one", static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[0].message->get())
                                   .payload());
          CASE_EXPECT_EQ("two", static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(*output[1].message->get())
                                   .payload());
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
            CASE_EXPECT_EQ(200, static_cast<int>(
                                    static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(
                                        *output[0].message->get())
                                        .id()));
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
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::remove_by_index(
            ctx, kTestDbChannel, "ut:kl:1", gsl::span<uint64_t>{remove_indexes}));
        CASE_EXPECT_EQ(0, res);

        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:1", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        if (!output.empty() && output[0].message) {
          CASE_EXPECT_EQ("one-v2", static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(
                                       *output[0].message->get())
                                       .payload());
        }

        // max_list_length trims the oldest entries.
        entry->set_payload("three");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, kTestDbChannel, "ut:kl:1", 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::get_all(ctx, kTestDbChannel, "ut:kl:1", output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        if (!output.empty() && output[0].message) {
          CASE_EXPECT_EQ("three", static_cast<const rpc_unit_test::RpcUnitTestListEntry &>(
                                      *output[0].message->get())
                                      .payload());
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

CASE_TEST(rpc_unit_test, db_ttl_expiry_and_remove_all) {
  atsf4g::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  test.db().set_now(std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}});

  auto task = test.run_task(
      "db_ttl", std::chrono::seconds{2}, [&test](rpc::context &ctx) -> rpc::result_code_type {
        rpc::shared_message<rpc_unit_test::RpcUnitTestTable> table{ctx};
        table->set_name("ttl");
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, kTestDbChannel, "ut:kv:ttl", rpc::shared_abstract_message<google::protobuf::Message>{table},
            nullptr));
        CASE_EXPECT_EQ(0, res);

        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::set_ttl(ctx, kTestDbChannel, "ut:kv:ttl", 60));
        CASE_EXPECT_EQ(0, res);

        auto output = atfw::util::memory::make_strong_rc<db_key_value_message_result_t>();
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
            ctx, kTestDbChannel, "ut:kv:ttl", rpc::shared_abstract_message<google::protobuf::Message>{table},
            nullptr));
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
  CASE_EXPECT_EQ(2, static_cast<int>(test.db().calls(op_type::set_ttl)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls(op_type::remove_ttl)));
  CASE_EXPECT_EQ(1, static_cast<int>(test.db().calls(op_type::remove_all)));

  CASE_EXPECT_EQ(0, test.stop());
}
