// Copyright 2026 atframework
//
// transaction_manager::stop() poisons mutable_transaction() for the whole process lifetime
// (UNIT_TEST_EXECUTION_PLAN.md section 4.4, stop row). It lives in a dedicated executable so the
// shutdown state cannot leak into any other coordinator case.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/config/dtcoordsvr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <string>
#include <vector>
#include <utility>

#include "dt_test_common.h"  // NOLINT(build/include_subdir)
#include "logic/transaction_manager.h"
#include "rpc/db/local_db_interface.atfw.gen.h"

namespace {
void setup_dtcoordsvr_config_loader() {
  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app& app_, logic_config&, logic_config::server_instance_config_ptr& to) {
        auto config_ptr =
            atfw::component::memory::stl::make_strong_rc<atfw::distributed_system::config::dtcoordsvr_cfg>();
        config_ptr->set_lru_max_cache_count(8);
        app_.parse_configures_into(*config_ptr, "dtcoordsvr", "ATAPP_DTCOORDSVR");
        to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });
}
}  // namespace

CASE_TEST(component_dtcoordsvr_stop, stop_poisons_mutable_transaction_for_process_lifetime) {
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

  auto task = test.run_task("dtcoordsvr_stop", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
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

    // stop() is idempotent.
    transaction_manager::me()->stop();
    transaction_manager::me()->stop();

    // After stop every fetch returns shutdown.
    transaction_manager::transaction_ptr_type poisoned;
    res = RPC_AWAIT_CODE_RESULT(transaction_manager::me()->mutable_transaction(ctx, metadata, poisoned));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN, res);
    CASE_EXPECT_FALSE(!!poisoned);

    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}
