// Copyright 2026 atframework
//
// Short-transaction stress case (UNIT_TEST_EXECUTION_PLAN.md section 7.1): 1,000 rounds of the
// client submit pipeline (coordinator create + participant prepare + coordinator commit + terminal
// notification) with periodic invariant checks that the RPC counters grow linearly and no round
// leaks failed participants. The participator-side container/lock leak checks live in the
// participator unit-test target (full resolve lifecycle coverage). Labelled stress so the default
// fast regression is not slowed down.

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/distributed_transaction.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <rpc/rpc_context.h>
#include <std/explicit_declare.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "dt_test_common.h"
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "transaction_client_handle.h"

namespace {
using atframework::distributed_system::EnDistibutedTransactionStatus;
using atframework::distributed_system::SSDistributeTransactionCommitReq;
using atframework::distributed_system::SSDistributeTransactionCommitRsp;
using atframework::distributed_system::SSDistributeTransactionCreateReq;
using atfw::distributed_system::SSDistributeTransactionCreateRsp;
using atframework::distributed_system::transaction_client_handle;

constexpr int kStressRounds = 1000;
constexpr int kBatchRounds = 50;
constexpr int kBatchCount = kStressRounds / kBatchRounds;
}  // namespace

CASE_TEST(component_distributed_transaction_stress, thousand_short_transactions) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  auto create_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_create(),
      SSDistributeTransactionCreateReq::descriptor()->full_name(),
      SSDistributeTransactionCreateRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        RPC_RETURN_CODE(0);
      });
  auto commit_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      SSDistributeTransactionCommitReq::descriptor()->full_name(),
      SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        auto& typed_request = static_cast<const SSDistributeTransactionCommitReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionCommitRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        typed_response.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule);

  // Recorder vtable: every prepare/commit notification succeeds, so each round is
  // create -> prepare -> coordinator commit -> commit notification.
  int prepare_calls = 0;
  int notify_calls = 0;
  auto client_vtable = atfw::component::memory::stl::make_strong_rc<transaction_client_handle::vtable_type>();
  client_vtable->prepare_participator = [&prepare_calls](
                                             rpc::context&, transaction_client_handle&,
                                             const transaction_client_handle::storage_type&,
                                             const transaction_client_handle::participator_type&,
                                             transaction_client_handle::transaction_participator_failure_reason&)
                                             -> rpc::result_code_type {
    ++prepare_calls;
    RPC_RETURN_CODE(0);
  };
  client_vtable->commit_participator = [&notify_calls](rpc::context&, transaction_client_handle&,
                                                       const transaction_client_handle::storage_type&,
                                                       const transaction_client_handle::participator_type&)
                                                       -> rpc::result_code_type {
    ++notify_calls;
    RPC_RETURN_CODE(0);
  };
  client_vtable->reject_participator = [](rpc::context&, transaction_client_handle&,
                                          const transaction_client_handle::storage_type&,
                                          const transaction_client_handle::participator_type&) -> rpc::result_code_type {
    RPC_RETURN_CODE(0);
  };
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(client_vtable);

  transaction_client_handle::transaction_options client_options;
  client_options.resolve_retry_interval = std::chrono::milliseconds{2};
  client_options.timeout = std::chrono::seconds{60};

  bool batch_failed = false;
  for (int batch = 0; batch < kBatchCount && !batch_failed; ++batch) {
    auto task = test.run_task(
        "stress_rounds", std::chrono::seconds{60},
        [&client, &client_options](rpc::context& ctx) -> rpc::result_code_type {
          for (int i = 0; i < kBatchRounds; ++i) {
            transaction_client_handle::storage_ptr_type storage;
            int32_t res = RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options));
            if (res < 0) {
              RPC_RETURN_CODE(res);
            }
            atframework::distributed_system::transaction_participator_failure_reason sample_data;
            res = client->add_participator(ctx, storage, "stress-p", sample_data);
            if (res < 0) {
              RPC_RETURN_CODE(res);
            }

            std::unordered_set<std::string> prepared;
            std::unordered_set<std::string> failed;
            res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
            if (res < 0) {
              RPC_RETURN_CODE(res);
            }
            if (prepared.size() != 1 || !failed.empty()) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
            }
          }
          RPC_RETURN_CODE(0);
        });
    auto result = test.wait(task, std::chrono::seconds{90});
    if (!result.task_exited || 0 != result.result_code) {
      CASE_MSG_INFO() << "stress batch " << batch << " failed: task_exited=" << result.task_exited
                      << " result=" << result.result_code << " diagnostic=" << result.diagnostic << std::endl;
      batch_failed = true;
      break;
    }

    // Linear-growth invariant: every round produced exactly one prepare, one coordinator create
    // and one coordinator commit so far; nothing accumulates per round beyond that.
    const int finished_rounds = (batch + 1) * kBatchRounds;
    if (prepare_calls != finished_rounds || notify_calls != finished_rounds ||
        static_cast<int>(test.ss().calls(rpc::transaction::packer::get_full_name_of_create())) != finished_rounds ||
        static_cast<int>(test.ss().calls(rpc::transaction::packer::get_full_name_of_commit())) != finished_rounds) {
      CASE_MSG_INFO() << "stress invariant failed at batch " << batch << ": prepare=" << prepare_calls
                      << " notify=" << notify_calls
                      << " create=" << test.ss().calls(rpc::transaction::packer::get_full_name_of_create())
                      << " commit=" << test.ss().calls(rpc::transaction::packer::get_full_name_of_commit())
                      << " expected=" << finished_rounds << std::endl;
      batch_failed = true;
      break;
    }
  }
  CASE_EXPECT_FALSE(batch_failed);

  CASE_EXPECT_EQ(kStressRounds, prepare_calls);
  CASE_EXPECT_EQ(kStressRounds, notify_calls);
  CASE_EXPECT_EQ(kStressRounds, static_cast<int>(test.ss().calls(rpc::transaction::packer::get_full_name_of_create())));
  CASE_EXPECT_EQ(kStressRounds, static_cast<int>(test.ss().calls(rpc::transaction::packer::get_full_name_of_commit())));

  CASE_EXPECT_EQ(0, test.stop());
}
