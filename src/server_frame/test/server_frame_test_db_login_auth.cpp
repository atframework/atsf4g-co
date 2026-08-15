// Copyright 2026 atframework

#include <chrono>
#include <cstdint>

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include "frame/test_macros.h"
#include "rpc/db/global_db_interface.atfw.gen.h"
#include "rpc/db/local_db_interface.atfw.gen.h"

namespace {
bool start_db_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::db};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_uuid_allocator>();
  return true;
}
}  // namespace

// server_frame component: the generated login_auth DB API (CAS enabled) must drive the full CRUD
// lifecycle through the real hash_table path, including the EN_DB_OLD_VERSION conflict contract.
CASE_TEST(server_frame_unit_test, db_login_auth_generated_api_crud_and_cas_conflict) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task("login_auth_crud", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    // Missing record reads as EN_DB_RECORD_NOT_FOUND.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
    uint64_t version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-crud", *record, version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, res);

    // First replace of a versionless record succeeds and starts the CAS sequence at 1.
    record->set_open_id("openid-crud");
    record->set_user_id(1001);
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, record, version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    // CAS replace with the current version bumps it.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> update{ctx};
    update->set_open_id("openid-crud");
    update->set_user_id(1002);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, update, version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(version));

    // Stale CAS: rejected with EN_DB_OLD_VERSION and the stored version is written back.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> stale{ctx};
    stale->set_open_id("openid-crud");
    stale->set_user_id(9999);
    uint64_t stale_version = 1;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, stale, stale_version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
    CASE_EXPECT_EQ(2, static_cast<int>(stale_version));

    // The conflicting write did not land.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> restored{ctx};
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-crud", *restored, version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1002, static_cast<int>(restored->user_id()));
    CASE_EXPECT_EQ(2, static_cast<int>(version));

    // Expected version 0 ignores the CAS check: the replace force-updates regardless of the stored
    // version and returns the new one (bumped from the stored version, 2 -> 3).
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> forced{ctx};
    forced->set_open_id("openid-crud");
    forced->set_user_id(1003);
    uint64_t forced_version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(ctx, forced, forced_version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(forced_version));

    // The forced write landed and is readable with the new version.
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-crud", *restored, version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1003, static_cast<int>(restored->user_id()));
    CASE_EXPECT_EQ(3, static_cast<int>(version));

    // remove_all clears the record.
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::remove_all(ctx, "openid-crud"));
    CASE_EXPECT_EQ(0, res);
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, "openid-crud", *restored, version));
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

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: uuid_allocator sequences are independent per (major, minor, path) and
// monotonic within one sequence.
CASE_TEST(server_frame_unit_test, db_uuid_allocator_independent_monotonic_sequences) {
  atfw::testing::runtime test;
  if (!start_db_runtime(test)) {
    return;
  }

  auto task = test.run_task(
      "uuid_allocator_sequences", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        uint64_t value = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::uuid_allocator::inc_field_auto_inc_id(ctx, 1, 1, 0, value));
        CASE_EXPECT_EQ(0, res);
        uint64_t first = value;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::uuid_allocator::inc_field_auto_inc_id(ctx, 1, 1, 0, value));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(value > first);

        // A different (major, minor, path) tuple has its own sequence.
        uint64_t other = 0;
        res = RPC_AWAIT_CODE_RESULT(rpc::db::uuid_allocator::inc_field_auto_inc_id(ctx, 1, 2, 0, other));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(static_cast<int64_t>(first), static_cast<int64_t>(other));
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
