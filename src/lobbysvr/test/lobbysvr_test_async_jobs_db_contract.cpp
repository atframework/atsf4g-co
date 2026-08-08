// Copyright 2026 atframework

// Sampling contract test for the lobbysvr async_jobs SDK DB consumer path (see
// doc/docs/development/rpc-unit-test.md): async_jobs::add_jobs/get_jobs/del_jobs go through the real generated KL table API
// (rpc::db::async_jobs) served by the mock in-memory backend, offline without Redis.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "frame/test_macros.h"
#include "rpc/async_jobs/async_jobs.h"
#include "rpc/db/local_db_interface.atfw.gen.h"

namespace {
constexpr int32_t kJobsType = 1002;  // EN_PAJT_NORMAL
constexpr uint64_t kUserId = 10001;
constexpr uint32_t kZoneId = 1;
}  // namespace

CASE_TEST(component_lobbysvr, async_jobs_db_contract) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::db};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_user_async_jobs>();

  auto task = test.run_task("async_jobs_flow", std::chrono::seconds{4}, [](rpc::context &ctx) -> rpc::result_code_type {
    // KL reads on a missing key report EN_DB_RECORD_NOT_FOUND with no entries.
    std::vector<rpc::db::async_jobs::table_user_async_jobs_list_message> out;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::get_jobs(ctx, kJobsType, kUserId, kZoneId, out));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, res);
    CASE_EXPECT_EQ(0, static_cast<int>(out.size()));
    res = 0;

    // Append two jobs through the SDK API into the in-memory backend.
    for (int i = 0; i < 2; ++i) {
      rpc::shared_message<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data> job{ctx};
      job->mutable_debug_message();
      res = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::add_jobs(ctx, kJobsType, kUserId, kZoneId, job));
      CASE_EXPECT_EQ(0, res);
    }

    out.clear();
    res = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::get_jobs(ctx, kJobsType, kUserId, kZoneId, out));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(out.size()));
    if (out.size() < 2) {
      RPC_RETURN_CODE(res);
    }
    // KL indexes are monotonic from 1 per key.
    CASE_EXPECT_EQ(1, static_cast<int>(out[0].list_index));
    CASE_EXPECT_EQ(2, static_cast<int>(out[1].list_index));
    CASE_EXPECT_EQ(kJobsType, (*out[0].message)->job_type());
    CASE_EXPECT_EQ(kUserId, static_cast<uint64_t>((*out[0].message)->user_id()));
    CASE_EXPECT_TRUE(!(*out[0].message)->job_data().action_uuid().empty());

    // Remove the first index; the other survives.
    res = RPC_AWAIT_CODE_RESULT(
        rpc::async_jobs::del_jobs(ctx, kJobsType, kUserId, kZoneId, std::vector<uint64_t>{out[0].list_index}));
    CASE_EXPECT_EQ(0, res);

    out.clear();
    res = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::get_jobs(ctx, kJobsType, kUserId, kZoneId, out));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(out.size()));
    if (!out.empty()) {
      CASE_EXPECT_EQ(2, static_cast<int>(out[0].list_index));
    }
    RPC_RETURN_CODE(res);
  });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Engine history: 2 KL adds + 1 remove_by_index + 3 get_all on the async_jobs table.
  CASE_EXPECT_EQ(2,
                 static_cast<int>(test.db().calls("async_jobs", atframework::testing::mock_db::op_type::kl_add_index)));
  CASE_EXPECT_EQ(
      1, static_cast<int>(test.db().calls("async_jobs", atframework::testing::mock_db::op_type::kl_remove_by_index)));
  CASE_EXPECT_EQ(3,
                 static_cast<int>(test.db().calls("async_jobs", atframework::testing::mock_db::op_type::kl_get_all)));

  CASE_EXPECT_EQ(0, test.stop());
}
