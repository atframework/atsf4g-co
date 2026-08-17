// Copyright 2026 atframework

// Sampling contract test for the distributed transaction SDK SS consumer path (see
// doc/docs/development/rpc-unit-test.md): rpc::transaction_api::create_transaction resolves the
// dtcoordsvr node by consistent hash over the real discovery index and issues
// atframework.distributed_system.DtcoordsvrService/create; the SS mock engine answers it offline.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <chrono>

#include "config/extern_service_types.h"
#include "frame/test_macros.h"
#include "logic/logic_server_setup.h"
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "rpc/transaction/transaction_api.h"

CASE_TEST(component_distributed_transaction, transaction_sdk_create_contract) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};

  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  // The only dtcoordsvr node: consistent hash must select it. The HPA-patched scaling_ready selector
  // requires the hpa_scaling_ready=1 metadata label (see logic_hpa_controller and the rank sample).
  atfw::testing::mock_node node;
  node.set_id(0x1B0001)
      .set_name("unit-test-dtcoordsvr")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtCoordSvr))
      .set_type_name("dtcoordsvr")
      .set_zone_id(1)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    test.stop();
    return;
  }

  // Mock injection writes the global discovery set directly; the common-module discovery index only
  // replays existing nodes on reload (node events fire only from the etcd watch path).
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }

  auto rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_create(),
      atfw::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCreateRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view &request, google::protobuf::Message &) -> rpc::result_code_type {
        const auto &typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionCreateReq &>(request.body);
        CASE_EXPECT_EQ(0x1B0001, static_cast<int64_t>(request.target_node_id));
        CASE_EXPECT_EQ("txn-unit-test-1", typed_request.storage().metadata().transaction_uuid());
        CASE_EXPECT_TRUE(typed_request.storage().metadata().memory_only());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);
  if (!rule) {
    CASE_MSG_INFO() << "mock registration failed: " << test.ss().get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto task =
      test.run_task("transaction_create", std::chrono::seconds{3}, [](rpc::context &ctx) -> rpc::result_code_type {
        atfw::distributed_system::transaction_blob_storage storage;
        storage.mutable_metadata()->set_transaction_uuid("txn-unit-test-1");
        storage.mutable_metadata()->set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        storage.mutable_metadata()->set_memory_only(true);
        storage.mutable_configure()->set_resolve_max_times(1);

        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(ctx, storage));
        CASE_EXPECT_EQ(0, res);
        RPC_RETURN_CODE(res);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (task.empty()) {
    CASE_MSG_INFO() << "run_task failed: " << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::transaction::packer::get_full_name_of_create())));

  CASE_EXPECT_EQ(0, test.stop());
}
