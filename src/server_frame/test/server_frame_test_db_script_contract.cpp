// Copyright 2026 atframework

#include <chrono>
#include <cstdint>
#include <vector>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include "dispatcher/db_msg_dispatcher.h"
#include "frame/test_macros.h"
#include "rpc/db/hash_table.h"

namespace {
bool start_db_script_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::db};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();
  return true;
}

uint32_t db_script_channel() { return static_cast<uint32_t>(db_msg_dispatcher::me()->get_db_channel_type()); }

const PROJECT_NAMESPACE_ID::table_login_auth &cast_login_auth(
    const atfw::util::memory::strong_rc_ptr<rpc::shared_abstract_message<google::protobuf::Message>> &message) {
  return static_cast<const PROJECT_NAMESPACE_ID::table_login_auth &>(*message->get());
}
}  // namespace

// The Lua scripts embedded in db_msg_dispatcher (kCompareAndSetHashTable, kInsertHashTable,
// kAddListIndexHashTable) are the authoritative write contract of the hash-table backend. These
// cases pin that contract through observable behavior against the same rpc::db::hash_table
// primitives production code uses; mock_db (the offline in-memory mirror) must satisfy them too.
// When a script changes, re-verify these cases and update mock_db together.

// server_frame component: kCompareAndSetHashTable behavior contract. A read reports the stored
// version; an update carrying exactly that version is applied and bumps the stored version by one;
// an expected version of 0 ignores the CAS check and force-overwrites (the stored version still
// bumps from the real one); any other non-zero expected version fails with EN_DB_OLD_VERSION,
// writes the stored version back and leaves the record untouched.
CASE_TEST(server_frame_unit_test, db_script_cas_version_contract) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_script_cas", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    const uint32_t channel = db_script_channel();
    const gsl::string_view key = "ut:kv:script-cas";
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
    record->set_open_id("openid-script-cas");
    record->set_user_id(1001);

    // A missing record accepts any expected version and starts the CAS sequence at 1.
    uint64_t version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    // The read reports exactly the version the write returned (read/write version consistency).
    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1001, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // An update carrying the version the read reported is applied, and the stored version really
    // changes: 1 -> 2.
    record->set_user_id(1002);
    version = output->version;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1002, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // A stale expected version (1 while 2 is stored): rejected with EN_DB_OLD_VERSION, the stored
    // version is written back and the conflicting write does not land.
    record->set_user_id(9999);
    version = 1;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
    CASE_EXPECT_EQ(2, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1002, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // Expected version 0 ignores the CAS check: the versioned record is overwritten regardless of
    // its stored version, the write lands and the stored version still bumps from the real one
    // (2 -> 3), never from the expected one.
    record->set_user_id(1003);
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1003, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(3, static_cast<int>(test.db().get_version("ut:kv:script-cas")));
  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: kInsertHashTable behavior contract. Only a key without a stored CAS
// version can be inserted; a duplicate fails with EN_DB_KEY_EXISTS (the caller's mapping of the
// script's CAS_FAILED reply), reports the stored version and leaves the record untouched; a record
// written only by the unversioned set still counts as absent for the script and accepts the insert.
CASE_TEST(server_frame_unit_test, db_script_insert_only_contract) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task =
      test.run_task("db_script_insert", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        const uint32_t channel = db_script_channel();
        const gsl::string_view key = "ut:kv:script-insert";
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
        record->set_open_id("openid-script-insert");
        record->set_user_id(2001);

        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
            ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));

        // The record now carries a CAS version: the script reports CAS_FAILED, which the caller
        // maps to EN_DB_KEY_EXISTS with the stored version written back.
        record->set_user_id(2002);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
            ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, version));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));

        auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output->version));
        if (output->message) {
          CASE_EXPECT_EQ(2001, static_cast<int>(cast_login_auth(output->message).user_id()));
        } else {
          CASE_EXPECT_TRUE(false);
        }

        // A record written only by the unversioned set has no CAS_VERSION field, so the script still
        // sees version 0 and the insert is accepted into it (the version sequence starts at 1).
        const gsl::string_view unversioned_key = "ut:kv:script-insert-unversioned";
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> unversioned{ctx};
        unversioned->set_open_id("openid-script-insert-unversioned");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, channel, unversioned_key, rpc::shared_abstract_message<google::protobuf::Message>{unversioned},
            nullptr));
        CASE_EXPECT_EQ(0, res);

        unversioned->set_user_id(3001);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
            ctx, channel, unversioned_key, rpc::shared_abstract_message<google::protobuf::Message>{unversioned},
            version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: every write path (plain HSET, the CAS script and the insert script) HSETs
// only the fields present in the store message, so fields absent from a partial store survive.
CASE_TEST(server_frame_unit_test, db_script_set_merges_present_fields) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_script_merge", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    const uint32_t channel = db_script_channel();
    const gsl::string_view key = "ut:kv:script-merge";
    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();

    // Unversioned HSET: a partial store keeps the fields of the previous one.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
    record->set_open_id("openid-script-merge");
    record->set_user_id(4001);
    record->set_access_token_code("token-1");
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, nullptr));
    CASE_EXPECT_EQ(0, res);

    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> partial{ctx};
    partial->set_user_id(4002);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{partial}, nullptr));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    if (output->message) {
      const auto &restored = cast_login_auth(output->message);
      CASE_EXPECT_EQ(4002, static_cast<int>(restored.user_id()));
      CASE_EXPECT_EQ("openid-script-merge", restored.open_id());
      CASE_EXPECT_EQ("token-1", restored.access_token_code());
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // The versioned CAS write merges the same way (the script HSETs the packed ARGV fields only):
    // bumping user_id alone must not drop open_id/access_token_code, and the version the read
    // reported stays usable for the next update.
    uint64_t version = 0;
    partial->set_user_id(4003);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{partial}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output->version));
    if (output->message) {
      const auto &restored = cast_login_auth(output->message);
      CASE_EXPECT_EQ(4003, static_cast<int>(restored.user_id()));
      CASE_EXPECT_EQ("openid-script-merge", restored.open_id());
      CASE_EXPECT_EQ("token-1", restored.access_token_code());
    } else {
      CASE_EXPECT_TRUE(false);
    }
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: kAddListIndexHashTable behavior contract. Indexes are 1-based, monotonic
// and never reused; update_by_index rewrites an entry in place without allocating an index; at
// capacity (data entries >= max_list_length, the counter field counts against the budget in the
// script) exactly one entry — the smallest index — is evicted before the new one is appended;
// max_list_length 0 keeps a single entry.
CASE_TEST(server_frame_unit_test, db_script_kl_add_index_contract) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_script_kl_add", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    const uint32_t channel = db_script_channel();
    const gsl::string_view key = "ut:kl:script-add";
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> entry{ctx};
    std::vector<db_key_list_message_result_t> output;

    // First allocated index is 1 and the sequence is monotonic.
    entry->set_access_token_code("one");
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, channel, key, 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);
    entry->set_access_token_code("two");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, channel, key, 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);
    entry->set_access_token_code("three");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, channel, key, 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(output.size()));
    for (const auto &item : output) {
      CASE_EXPECT_TRUE(item.list_index >= 1 && item.list_index <= 3);
    }

    // update_by_index rewrites the entry in place: same index, new data, no index allocated.
    entry->set_access_token_code("one-v2");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(
        ctx, channel, key, 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);
    uint64_t updated[] = {1};
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
        ctx, channel, key, gsl::span<uint64_t>{updated}, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
    if (output.size() == 1) {
      CASE_EXPECT_EQ(1, static_cast<int>(output[0].list_index));
      if (output[0].message) {
        CASE_EXPECT_EQ("one-v2", cast_login_auth(output[0].message).access_token_code());
      }
    }

    // Reorder so the smallest index is not at the oldest insertion position: remove index 1, then
    // re-create it via update_by_index (appended last).
    uint64_t removed[] = {1};
    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::hash_table::key_list::remove_by_index(ctx, channel, key, gsl::span<uint64_t>{removed}));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(
        ctx, channel, key, 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    // At capacity (3 entries, max_list_length 3) the add evicts the smallest index (1), not the
    // oldest insertion position, and appends a fresh monotonic index (4) — the evicted index is
    // never reused.
    entry->set_access_token_code("four");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, channel, key, 3, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    uint64_t probe[] = {1, 2, 3, 4};
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
        ctx, channel, key, gsl::span<uint64_t>{probe}, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(4, static_cast<int>(output.size()));
    if (output.size() == 4) {
      CASE_EXPECT_FALSE(!!output[0].message);  // 1: evicted and never reused
      CASE_EXPECT_TRUE(!!output[1].message);   // 2
      CASE_EXPECT_TRUE(!!output[2].message);   // 3
      CASE_EXPECT_TRUE(!!output[3].message);   // 4: appended
      if (output[3].message) {
        CASE_EXPECT_EQ("four", cast_login_auth(output[3].message).access_token_code());
      }
    }

    // max_list_length 0: the counter field always counts against the budget, so the list keeps a
    // single entry and every add replaces the previous one.
    const gsl::string_view max0_key = "ut:kl:script-max0";
    entry->set_access_token_code("solo-1");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, channel, max0_key, 0, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);
    entry->set_access_token_code("solo-2");
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
        ctx, channel, max0_key, 0, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, channel, max0_key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
    if (output.size() == 1 && output[0].message) {
      CASE_EXPECT_EQ("solo-2", cast_login_auth(output[0].message).access_token_code());
      CASE_EXPECT_EQ(2, static_cast<int>(output[0].list_index));
    }
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(6, static_cast<int>(test.db().calls(atfw::testing::mock_db::op_type::kl_add_index)));
  CASE_EXPECT_EQ(0, test.stop());
}
