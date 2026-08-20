// Copyright 2026 atframework
//
// rpc::transaction_api unit tests (UNIT_TEST_EXECUTION_PLAN.md section 4.1): initialize/merge/pack
// pure functions, the chrono protocol boundary (DT-012/DT-013/DT-017), single-node RPC paths and
// the replication R-of-N quorum accounting (DT-003).

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

#include <std/explicit_declare.h>

#include <gsl/select-gsl.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "dt_test_common.h"  // NOLINT(build/include_subdir)
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "rpc/transaction/transaction_api.h"
#include "utility/protobuf_mini_dumper.h"

namespace {
using atfw::distributed_system::EnDistibutedTransactionStatus;
using atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq;
using atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp;
using atfw::distributed_system::SSDistributeTransactionCommitReq;
using atfw::distributed_system::SSDistributeTransactionCommitRsp;
using atfw::distributed_system::SSDistributeTransactionCreateReq;
using atfw::distributed_system::SSDistributeTransactionCreateRsp;
using atfw::distributed_system::SSDistributeTransactionQueryReq;
using atfw::distributed_system::SSDistributeTransactionQueryRsp;
using atfw::distributed_system::SSDistributeTransactionRemoveReq;
using atfw::distributed_system::SSDistributeTransactionRemoveRsp;
using atfw::distributed_system::transaction_blob_storage;
using atfw::distributed_system::transaction_metadata;
using atfw::distributed_system::transaction_participator;

// DT-017: the shared conversion helpers must return google.protobuf.Duration/Timestamp value types
// and the component must not keep private duplicate conversion helpers.
static_assert(std::is_same<google::protobuf::Duration,
                           decltype(protobuf_from_chrono_duration(std::chrono::seconds{1}))>::value,
              "protobuf_from_chrono_duration must return google.protobuf.Duration");
static_assert(std::is_same<google::protobuf::Duration,
                           decltype(protobuf_from_chrono_duration(std::chrono::milliseconds{1500}))>::value,
              "protobuf_from_chrono_duration must keep the Duration return type for all input reps");
static_assert(
    std::is_same<std::chrono::system_clock::duration,
                  decltype(protobuf_to_chrono_duration(google::protobuf::Duration{}))>::value,
    "protobuf_to_chrono_duration must default to system_clock::duration");

template <class TRep, class TPeriod>
void expect_duration_round_trip(std::chrono::duration<TRep, TPeriod> input) {
  // The public boundary takes system_clock::duration; sub-period inputs are converted first (the
  // conversion itself must not lose relative ordering).
  auto sys_input = std::chrono::duration_cast<std::chrono::system_clock::duration>(input);
  google::protobuf::Duration proto_value = protobuf_from_chrono_duration(sys_input);
  auto round_trip = protobuf_to_chrono_duration(proto_value);
  CASE_EXPECT_EQ(std::chrono::duration_cast<std::chrono::nanoseconds>(sys_input).count(),
                 std::chrono::duration_cast<std::chrono::nanoseconds>(round_trip).count());
}

}  // namespace

// ============ chrono boundary: DT-012 / DT-013 / DT-017 ============

CASE_TEST(component_distributed_transaction_api, chrono_duration_helper_returns_duration_dt017) {
  expect_duration_round_trip(std::chrono::system_clock::duration::zero());
  expect_duration_round_trip(std::chrono::seconds{7});
  expect_duration_round_trip(std::chrono::milliseconds{-1500});
  expect_duration_round_trip(std::chrono::microseconds{1500});
  expect_duration_round_trip(std::chrono::nanoseconds{1});
  expect_duration_round_trip(std::chrono::nanoseconds{-1});
  expect_duration_round_trip(std::chrono::hours{3});

  // Sub-second values must not lose precision or be normalized into whole seconds.
  google::protobuf::Duration sub_second = protobuf_from_chrono_duration(std::chrono::milliseconds{250});
  CASE_EXPECT_EQ(0, sub_second.seconds());
  CASE_EXPECT_EQ(250000000, sub_second.nanos());

  google::protobuf::Duration negative_sub_second = protobuf_from_chrono_duration(std::chrono::milliseconds{-250});
  CASE_EXPECT_EQ(0, negative_sub_second.seconds());
  CASE_EXPECT_EQ(-250000000, negative_sub_second.nanos());
}

CASE_TEST(component_distributed_transaction_api, timestamp_exact_nanosecond_carry_dt013) {
  // 999999999ns + 1ns must carry into the next second exactly (no 1e9 nanos field).
  google::protobuf::Duration carried =
      protobuf_from_chrono_duration(std::chrono::nanoseconds{999999999} + std::chrono::nanoseconds{1});
  CASE_EXPECT_EQ(1, carried.seconds());
  CASE_EXPECT_EQ(0, carried.nanos());

  google::protobuf::Duration negative_carried =
      protobuf_from_chrono_duration(std::chrono::nanoseconds{-999999999} + std::chrono::nanoseconds{-1});
  CASE_EXPECT_EQ(-1, negative_carried.seconds());
  CASE_EXPECT_EQ(0, negative_carried.nanos());

  // Whole-second time points round-trip exactly.
  const auto epoch_seconds = std::chrono::system_clock::time_point{std::chrono::seconds{1789000000}};
  google::protobuf::Timestamp ts_value = protobuf_from_system_clock(epoch_seconds);
  CASE_EXPECT_EQ(1789000000, ts_value.seconds());
  CASE_EXPECT_EQ(0, ts_value.nanos());
  CASE_EXPECT_EQ(epoch_seconds, protobuf_to_system_clock(ts_value));

  // Now + timeout keeps the prepare timestamp ordering and sub-second remainder.
  auto now = std::chrono::system_clock::now();
  google::protobuf::Timestamp now_ts = protobuf_from_system_clock(now);
  CASE_EXPECT_EQ(now, protobuf_to_system_clock(now_ts));
}

// ============ initialize_new_transaction ============

CASE_TEST(component_distributed_transaction_api, initialize_new_transaction_defaults_dt012) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task = test.run_task("initialize_defaults", std::chrono::seconds{3},
                            [](rpc::context& ctx) -> rpc::result_code_type {
                              transaction_blob_storage storage;
                              int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
                                  ctx, storage, std::chrono::system_clock::duration::zero()));
                              CASE_EXPECT_EQ(0, res);
                              CASE_EXPECT_FALSE(storage.metadata().transaction_uuid().empty());
                              CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED,
                                             storage.metadata().status());
                              CASE_EXPECT_FALSE(storage.metadata().memory_only());
                              CASE_EXPECT_FALSE(storage.configure().force_commit());
                              // zero timeout falls back to 10s
                              auto expire = protobuf_to_system_clock(storage.metadata().expire_timepoint());
                              auto prepare = protobuf_to_system_clock(storage.metadata().prepare_timepoint());
                              CASE_EXPECT_LE(std::chrono::seconds(9), expire - prepare);
                              CASE_EXPECT_LE(expire - prepare, std::chrono::seconds(11));
                              // default configure fallbacks
                              CASE_EXPECT_EQ(3, storage.configure().resolve_max_times());
                              CASE_EXPECT_EQ(3, storage.configure().lock_retry_max_times());
                              CASE_EXPECT_EQ(std::chrono::seconds{10},
                                             protobuf_to_chrono_duration(storage.configure().resolve_retry_interval()));
                              CASE_EXPECT_EQ(std::chrono::milliseconds{32},
                                             protobuf_to_chrono_duration(storage.configure().lock_wait_interval_min()));
                              CASE_EXPECT_EQ(std::chrono::milliseconds{256},
                                             protobuf_to_chrono_duration(storage.configure().lock_wait_interval_max()));
                              // R=0/N=0: no replication ids are written
                              CASE_EXPECT_EQ(0, storage.metadata().replicate_read_count());
                              CASE_EXPECT_EQ(0, storage.metadata().replicate_node_server_id_size());
                              RPC_RETURN_CODE(0);
                            });
  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_api, initialize_new_transaction_custom_options_dt012) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task = test.run_task("initialize_custom", std::chrono::seconds{3},
                            [](rpc::context& ctx) -> rpc::result_code_type {
                              transaction_blob_storage storage;
                              int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
                                  ctx, storage, std::chrono::milliseconds{1500}, 0, 0, true, true));
                              CASE_EXPECT_EQ(0, res);
                              CASE_EXPECT_TRUE(storage.metadata().memory_only());
                              CASE_EXPECT_TRUE(storage.configure().force_commit());
                              auto expire = protobuf_to_system_clock(storage.metadata().expire_timepoint());
                              auto prepare = protobuf_to_system_clock(storage.metadata().prepare_timepoint());
                              CASE_EXPECT_EQ(std::chrono::milliseconds{1500}, expire - prepare);

                              // negative timeout falls back to the 10s default, never a past timepoint
                              transaction_blob_storage negative_storage;
                              res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
                                  ctx, negative_storage, std::chrono::seconds{-5}));
                              CASE_EXPECT_EQ(0, res);
                              CASE_EXPECT_LE(std::chrono::seconds{9},
                                             protobuf_to_system_clock(negative_storage.metadata().expire_timepoint()) -
                                                 protobuf_to_system_clock(
                                                     negative_storage.metadata().prepare_timepoint()));

                              // UUIDs are unique across calls
                              CASE_EXPECT_NE(storage.metadata().transaction_uuid(),
                                             negative_storage.metadata().transaction_uuid());
                              RPC_RETURN_CODE(0);
                            });
  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_api, initialize_new_transaction_replication_nodes) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002, 0x1B0003}));

  auto task = test.run_task("initialize_replication", std::chrono::seconds{3},
                            [](rpc::context& ctx) -> rpc::result_code_type {
                              // N is clamped to the discovered node count and R to N
                              transaction_blob_storage storage;
                              int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
                                  ctx, storage, std::chrono::seconds{5}, 2, 5));
                              CASE_EXPECT_EQ(0, res);
                              CASE_EXPECT_EQ(2, storage.metadata().replicate_read_count());
                              CASE_EXPECT_EQ(3, storage.metadata().replicate_node_server_id_size());

                              // N<R is an invalid consistency request: no replication ids are set
                              transaction_blob_storage invalid_storage;
                              res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::initialize_new_transaction(
                                  ctx, invalid_storage, std::chrono::seconds{5}, 5, 2));
                              CASE_EXPECT_EQ(0, res);
                              CASE_EXPECT_EQ(0, invalid_storage.metadata().replicate_read_count());
                              CASE_EXPECT_EQ(0, invalid_storage.metadata().replicate_node_server_id_size());
                              RPC_RETURN_CODE(0);
                            });
  auto result = test.wait(task, std::chrono::seconds{6});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ merge_storage ============

CASE_TEST(component_distributed_transaction_api, merge_storage_blob_and_participator) {
  transaction_blob_storage output;
  transaction_blob_storage input;
  input.mutable_metadata()->set_transaction_uuid("merge-uuid-1");
  input.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
  input.mutable_metadata()->mutable_finish_timepoint()->set_seconds(100);
  input.mutable_configure()->set_resolve_max_times(7);
  input.mutable_configure()->mutable_lock_wait_interval_min()->set_nanos(32000000);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  transaction_participator& participator = (*input.mutable_participators())["pa"];
  participator.set_participator_key("pa");
  participator.set_participator_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);

  // Empty output adopts every present field of the input.
  rpc::transaction_api::merge_storage(output, input);
  CASE_EXPECT_EQ("merge-uuid-1", output.metadata().transaction_uuid());
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                 output.metadata().status());
  CASE_EXPECT_EQ(100, output.metadata().finish_timepoint().seconds());
  CASE_EXPECT_EQ(7, output.configure().resolve_max_times());
  CASE_EXPECT_EQ(32000000, output.configure().lock_wait_interval_min().nanos());
  CASE_EXPECT_EQ(1, output.participators().size());

  // Merge is monotonic for status and never drops already-merged fields.
  transaction_blob_storage later;
  later.mutable_metadata()->set_transaction_uuid("merge-uuid-1");
  later.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
  later.mutable_configure()->set_lock_retry_max_times(4);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  transaction_participator& participator_b = (*later.mutable_participators())["pb"];
  participator_b.set_participator_key("pb");
  participator_b.set_participator_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
  rpc::transaction_api::merge_storage(output, later);
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                 output.metadata().status());
  CASE_EXPECT_EQ(7, output.configure().resolve_max_times());  // kept from the first merge
  CASE_EXPECT_EQ(4, output.configure().lock_retry_max_times());
  CASE_EXPECT_EQ(2, output.participators().size());

  // Conflicting terminal statuses converge deterministically to the later one (COMMITED wins over
  // REJECTED by the max-status rule) without returning an error.
  transaction_blob_storage conflicting;
  conflicting.mutable_metadata()->set_transaction_uuid("merge-uuid-1");
  conflicting.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
  rpc::transaction_api::merge_storage(output, conflicting);
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                 output.metadata().status());

  // Participator-scoped merge: missing key does not touch the output.
  atfw::distributed_system::transaction_participator_storage participator_output;
  participator_output.mutable_metadata()->set_transaction_uuid("merge-uuid-1");
  rpc::transaction_api::merge_storage("missing", participator_output, output);
  CASE_EXPECT_EQ("merge-uuid-1", participator_output.metadata().transaction_uuid());
  CASE_EXPECT_FALSE(participator_output.has_participator_data());

  // Participator-scoped merge: existing key copies metadata/configure/any data.
  atfw::distributed_system::transaction_participator_storage participator_merged;
  rpc::transaction_api::merge_storage("pa", participator_merged, output);
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                 participator_merged.metadata().status());
  CASE_EXPECT_EQ(7, participator_merged.configure().resolve_max_times());
  CASE_EXPECT_EQ("merge-uuid-1", participator_merged.metadata().transaction_uuid());
}

// ============ pack_participator_request ============

CASE_TEST(component_distributed_transaction_api, pack_participator_request_variants) {
  transaction_blob_storage storage;
  storage.mutable_metadata()->set_transaction_uuid("pack-uuid-1");
  storage.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  storage.mutable_configure()->set_resolve_max_times(9);
  transaction_participator participator;
  participator.set_participator_key("pa");

  atfw::distributed_system::SSParticipatorTransactionPrepareReq prepare_req;
  rpc::transaction_api::pack_participator_request(prepare_req, storage, participator);
  CASE_EXPECT_EQ("pack-uuid-1", prepare_req.storage().metadata().transaction_uuid());
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                 prepare_req.storage().metadata().status());
  CASE_EXPECT_EQ(9, prepare_req.storage().configure().resolve_max_times());
  CASE_EXPECT_TRUE(prepare_req.storage().participator_data().type_url().empty());  // empty source Any

  atfw::distributed_system::SSParticipatorTransactionCommitReq commit_req;
  rpc::transaction_api::pack_participator_request(commit_req, storage, participator);
  // The normal commit notification carries the UUID only.
  CASE_EXPECT_EQ("pack-uuid-1", commit_req.transaction_uuid());

  atfw::distributed_system::SSParticipatorTransactionRejectReq reject_req;
  rpc::transaction_api::pack_participator_request(reject_req, storage, participator);
  CASE_EXPECT_EQ("pack-uuid-1", reject_req.transaction_uuid());
  CASE_EXPECT_FALSE(reject_req.has_storage());  // normal reject carries the UUID only

  // force_commit reject carries the full undo data.
  storage.mutable_configure()->set_force_commit(true);
  atfw::distributed_system::SSParticipatorTransactionRejectReq force_reject_req;
  rpc::transaction_api::pack_participator_request(force_reject_req, storage, participator);
  CASE_EXPECT_EQ("pack-uuid-1", force_reject_req.transaction_uuid());
  CASE_EXPECT_TRUE(force_reject_req.has_storage());
  CASE_EXPECT_EQ(9, force_reject_req.storage().configure().resolve_max_times());
}

// ============ generated packer round trip ============

CASE_TEST(component_distributed_transaction_api, generated_packer_round_trip) {
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/query",
                 std::string{rpc::transaction::packer::get_full_name_of_query()});
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/create",
                 std::string{rpc::transaction::packer::get_full_name_of_create()});
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/commit",
                 std::string{rpc::transaction::packer::get_full_name_of_commit()});
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/reject",
                 std::string{rpc::transaction::packer::get_full_name_of_reject()});
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/commit_participator",
                 std::string{rpc::transaction::packer::get_full_name_of_commit_participator()});
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/reject_participator",
                 std::string{rpc::transaction::packer::get_full_name_of_reject_participator()});
  CASE_EXPECT_EQ("atframework.distributed_system.DtcoordsvrService/remove",
                 std::string{rpc::transaction::packer::get_full_name_of_remove()});

  SSDistributeTransactionCommitReq commit_req;
  commit_req.mutable_metadata()->set_transaction_uuid("packer-uuid-1");
  commit_req.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
  std::string packed;
  CASE_EXPECT_TRUE(rpc::transaction::packer::pack_commit(packed, commit_req));
  SSDistributeTransactionCommitReq unpacked;
  CASE_EXPECT_TRUE(rpc::transaction::packer::unpack_commit(packed, unpacked));
  CASE_EXPECT_EQ("packer-uuid-1", unpacked.metadata().transaction_uuid());
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                 unpacked.metadata().status());

  // A malformed body is rejected by unpack.
  CASE_EXPECT_FALSE(rpc::transaction::packer::unpack_commit(std::string{"\xff\xff\xff\xff"}, unpacked));
}

// ============ single node RPC paths ============

CASE_TEST(component_distributed_transaction_api, query_transaction_single_node_paths) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  transaction_blob_storage input;
  dt_test::make_prepared_storage(input, "api-query-uuid-1", {"pa"});
  transaction_metadata query_metadata = input.metadata();

  auto success_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      SSDistributeTransactionQueryReq::descriptor()->full_name(),
      SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [&input](const atfw::testing::ss_request_view&,
          google::protobuf::Message& response) -> rpc::result_code_type {
        auto& typed_response = static_cast<SSDistributeTransactionQueryRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_storage(), input);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!success_rule);

  auto task = test.run_task("api_query", std::chrono::seconds{4},
      [query_metadata](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage output;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::query_transaction(ctx, query_metadata, output));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ("api-query-uuid-1", output.metadata().transaction_uuid());
    CASE_EXPECT_EQ(1, output.participators().size());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_query()));

  // Business error response propagates the coordinator error code as-is.
  success_rule.reset();
  CASE_EXPECT_FALSE(!!success_rule);
  auto error_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_query(),
                                         PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  CASE_EXPECT_TRUE(!!error_rule);
  auto error_task = test.run_task(
      "api_query_notfound", std::chrono::seconds{4}, [query_metadata](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage output;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::query_transaction(ctx, query_metadata, output));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, res);
        RPC_RETURN_CODE(0);
      });
  auto error_result = test.wait(error_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(error_result.task_exited);
  CASE_EXPECT_EQ(0, error_result.result_code);

  // No available coordinator node: discovery misses and the call fails fast.
  error_rule.reset();
  test.discovery().remove_node(0x1B0001);
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  auto lost_task = test.run_task(
      "api_query_no_node", std::chrono::seconds{4}, [query_metadata](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage output;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::query_transaction(ctx, query_metadata, output));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND, res);
        RPC_RETURN_CODE(0);
      });
  auto lost_result = test.wait(lost_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(lost_result.task_exited);
  CASE_EXPECT_EQ(0, lost_result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_api, create_transaction_param_guards_and_single_shot) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int create_calls = 0;
  auto rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_create(),
      atfw::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCreateRsp::descriptor()->full_name(),
      [&create_calls](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        ++create_calls;
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);

  auto task = test.run_task("api_create", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
    // resolve_max_times == 0 is rejected before any RPC
    transaction_blob_storage invalid_storage;
    invalid_storage.mutable_metadata()->set_transaction_uuid("api-create-invalid");
    invalid_storage.mutable_metadata()->set_status(
        EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    invalid_storage.mutable_configure()->set_resolve_max_times(0);
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(ctx, invalid_storage));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, res);

    // non-PREPARED status is rejected before any RPC
    invalid_storage.mutable_configure()->set_resolve_max_times(3);
    invalid_storage.mutable_metadata()->set_status(
        EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED);
    res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(ctx, invalid_storage));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, res);

    // success
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "api-create-ok", {"pa"});
    res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(ctx, storage));
    CASE_EXPECT_EQ(0, res);
    // the caller-owned request object keeps its content after the call (heap storage is released
    // back to the caller, not consumed)
    CASE_EXPECT_EQ("api-create-ok", storage.metadata().transaction_uuid());
    CASE_EXPECT_EQ(1, storage.participators().size());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, create_calls);  // exactly one RPC: the two parameter-guard failures sent nothing
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()));

  // EN_DB_OLD_VERSION is returned to the caller on a single call (no internal retry in the api
  // layer; bounded retries belong to the client handle).
  rule.reset();
  auto conflict_rule =
      test.ss().mock_error(rpc::transaction::packer::get_full_name_of_create(),
          PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
  CASE_EXPECT_TRUE(!!conflict_rule);
  auto conflict_task = test.run_task("api_create_conflict", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage storage;
    dt_test::make_prepared_storage(storage, "api-create-conflict", {"pa"});
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(ctx, storage));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
    RPC_RETURN_CODE(0);
  });
  auto conflict_result = test.wait(conflict_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(conflict_result.task_exited);
  CASE_EXPECT_EQ(0, conflict_result.result_code);
  // one success call above + one conflict call here
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_api, commit_reject_remove_single_shot_semantics) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  transaction_blob_storage storage;
  dt_test::make_prepared_storage(storage, "api-terminal-uuid-1", {"pa", "pb"});

  auto commit_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      atfw::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        auto& typed_response = static_cast<SSDistributeTransactionCommitRsp&>(response);
        typed_response.mutable_metadata()->set_transaction_uuid("api-terminal-uuid-1");
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!commit_rule);

  auto reject_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject(),
      atfw::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionRejectRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view&, google::protobuf::Message& response) -> rpc::result_code_type {
        auto& typed_response =
            static_cast<atfw::distributed_system::SSDistributeTransactionRejectRsp&>(response);
        typed_response.mutable_metadata()->set_transaction_uuid("api-terminal-uuid-1");
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!reject_rule);

  auto remove_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_remove(),
      SSDistributeTransactionRemoveReq::descriptor()->full_name(),
      SSDistributeTransactionRemoveRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND);
      });
  CASE_EXPECT_TRUE(!!remove_rule);

  auto task = test.run_task("api_terminal", std::chrono::seconds{4}, [](rpc::context& ctx) -> rpc::result_code_type {
    // commit merges the coordinator terminal metadata into the inout parameter
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-terminal-uuid-1");
    metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(ctx, metadata));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED, metadata.status());

    // reject reports the persisted terminal decision instead of the requested direction
    transaction_metadata reject_metadata;
    reject_metadata.set_transaction_uuid("api-terminal-uuid-1");
    reject_metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_transaction(ctx, reject_metadata));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   reject_metadata.status());

    // remove maps record-not-found to idempotent success
    res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction(ctx, reject_metadata));
    CASE_EXPECT_EQ(0, res);
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_remove()));

  // OLD_VERSION on commit/reject/remove is a single call returning the conflict, no internal retry.
  commit_rule.reset();
  reject_rule.reset();
  remove_rule.reset();
  auto conflict_commit = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_commit(),
                                              PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
  auto conflict_reject = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_reject(),
                                              PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
  auto conflict_remove = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_remove(),
                                              PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
  CASE_EXPECT_TRUE(!!conflict_commit && !!conflict_reject && !!conflict_remove);

  auto conflict_task = test.run_task("api_conflicts", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-terminal-uuid-1");
    metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(ctx, metadata)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_transaction(ctx, metadata)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction(ctx, metadata)));
    RPC_RETURN_CODE(0);
  });
  auto conflict_result = test.wait(conflict_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(conflict_result.task_exited);
  CASE_EXPECT_EQ(0, conflict_result.result_code);
  // one success call above + one conflict call here for each terminal RPC
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_remove()));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_api, remove_no_wait_one_way) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  transaction_metadata metadata;
  metadata.set_transaction_uuid("api-remove-nowait-1");
  metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);

  auto task = test.run_task("api_remove_nowait", std::chrono::seconds{4},
      [metadata](rpc::context& ctx) -> rpc::result_code_type {
    // empty UUID is rejected without sending
    transaction_metadata empty_metadata;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction_no_wait(ctx, empty_metadata)));

    // unmatched one-way sends are recorded and dropped with success by the default strict policy
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction_no_wait(ctx, metadata)));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  // The one-way send crosses the one-generation transport barrier: pump before counting history.
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_remove()));
  CASE_EXPECT_EQ(1, test.transport().outbound_count_to(0x1B0001));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_api, participator_terminal_rpc_passthrough) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  std::string commit_key;
  std::string reject_key;
  auto commit_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit_participator(),
      SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name(),
      SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name(),
      [&commit_key](const atfw::testing::ss_request_view& request,
                    google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionCommitParticipatorReq&>(request.body);
        commit_key = typed_request.participator_key();
        auto& typed_response = static_cast<SSDistributeTransactionCommitParticipatorRsp&>(response);
        typed_response.mutable_metadata()->set_transaction_uuid(typed_request.metadata().transaction_uuid());
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  auto reject_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject_participator(),
      atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp::descriptor()->full_name(),
      [&reject_key](const atfw::testing::ss_request_view& request,
                    google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq&>(request.body);
        reject_key = typed_request.participator_key();
        auto& typed_response =
            static_cast<atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp&>(response);
        typed_response.mutable_metadata()->set_transaction_uuid(typed_request.metadata().transaction_uuid());
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!commit_rule && !!reject_rule);

  auto task = test.run_task("api_participator_terminal", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-participator-uuid-1");
    metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_participator(ctx, "pa", metadata));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED, metadata.status());

    transaction_metadata reject_metadata;
    reject_metadata.set_transaction_uuid("api-participator-uuid-1");
    reject_metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING);
    res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_participator(ctx, "pb", reject_metadata));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   reject_metadata.status());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ("pa", commit_key);
  CASE_EXPECT_EQ("pb", reject_key);
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit_participator()));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject_participator()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-003: replication quorum counts only valid success responses ============

CASE_TEST(component_distributed_transaction_api, replication_counts_only_valid_success_dt003) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002, 0x1B0003}));

  // node1: business error; node2: success with an empty response body (invalid for commit, which
  // requires metadata); node3: valid COMMITED metadata. Only node3 is a valid success, 1 < R=2.
  atfw::testing::ss_rule_options node1_options;
  node1_options.match_node_id = 0x1B0001;
  auto node1_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_commit(),
                                         PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, node1_options);
  atfw::testing::ss_rule_options node2_options;
  node2_options.match_node_id = 0x1B0002;
  auto node2_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      atfw::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        RPC_RETURN_CODE(0);  // empty commit response: transport success, business invalid
      },
      node2_options);
  atfw::testing::ss_rule_options node3_options;
  node3_options.match_node_id = 0x1B0003;
  node3_options.delay_generations = 2;  // out-of-order: the valid response arrives last
  auto node3_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      atfw::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request, google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionCommitReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionCommitRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      },
      node3_options);
  CASE_EXPECT_TRUE(!!node1_rule && !!node2_rule && !!node3_rule);

  auto task = test.run_task("replication_quorum", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-replication-uuid-1");
    metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    metadata.set_replicate_read_count(2);
    metadata.add_replicate_node_server_id(0x1B0001);
    metadata.add_replicate_node_server_id(0x1B0002);
    metadata.add_replicate_node_server_id(0x1B0003);

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(ctx, metadata));
    // Only one valid success (< R=2): the call must fail, not fake a quorum from transport-level
    // successes (the empty body of node2 does not count). The exact code depends on which invalid
    // response was recorded last; what matters is the failure itself. A partially merged terminal
    // from fewer than R replicas is allowed to land in metadata, but the failure return code is
    // what keeps the client from treating it as a confirmed decision.
    CASE_EXPECT_TRUE(res < 0);
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(3, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));

  // With node2 also returning valid metadata, two valid successes satisfy R=2 and the merged
  // status is COMMITED even though the responses arrive out of order.
  node2_rule.reset();
  atfw::testing::ss_rule_options node2_valid_options;
  node2_valid_options.match_node_id = 0x1B0002;
  auto node2_valid_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      atfw::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request, google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionCommitReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionCommitRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      },
      node2_valid_options);
  CASE_EXPECT_TRUE(!!node2_valid_rule);

  auto quorum_task = test.run_task("replication_quorum_met", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-replication-uuid-1");
    metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    metadata.set_replicate_read_count(2);
    metadata.add_replicate_node_server_id(0x1B0001);
    metadata.add_replicate_node_server_id(0x1B0002);
    metadata.add_replicate_node_server_id(0x1B0003);
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(ctx, metadata));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED, metadata.status());
    RPC_RETURN_CODE(0);
  });
  auto quorum_result = test.wait(quorum_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(quorum_result.task_exited);
  CASE_EXPECT_EQ(0, quorum_result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ replication: query merges replica fields (4.1 query row) ============

CASE_TEST(component_distributed_transaction_api, query_transaction_replication_merge) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002, 0x1B0003}));

  transaction_metadata replication_metadata;
  replication_metadata.set_transaction_uuid("api-replication-query-1");
  replication_metadata.set_replicate_read_count(2);
  replication_metadata.add_replicate_node_server_id(0x1B0001);
  replication_metadata.add_replicate_node_server_id(0x1B0002);
  replication_metadata.add_replicate_node_server_id(0x1B0003);

  // node1: business error; node2: PREPARED with a finish timepoint and participator pa;
  // node3: COMMITED with transaction data. Two valid responses satisfy R=2 and the fields merge.
  atfw::testing::ss_rule_options error_options;
  error_options.match_node_id = 0x1B0001;
  auto error_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_query(),
                                         PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, error_options);
  atfw::testing::ss_rule_options node2_options;
  node2_options.match_node_id = 0x1B0002;
  auto node2_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      SSDistributeTransactionQueryReq::descriptor()->full_name(),
      SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionQueryRsp&>(response);
        auto* storage = typed_response.mutable_storage();
        storage->mutable_metadata()->set_transaction_uuid(typed_request.metadata().transaction_uuid());
        storage->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        storage->mutable_metadata()->mutable_finish_timepoint()->set_seconds(123);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        auto& participator = (*storage->mutable_participators())["pa"];
        participator.set_participator_key("pa");
        participator.set_participator_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        RPC_RETURN_CODE(0);
      },
      node2_options);
  atfw::testing::ss_rule_options node3_options;
  node3_options.match_node_id = 0x1B0003;
  auto node3_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      SSDistributeTransactionQueryReq::descriptor()->full_name(),
      SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionQueryRsp&>(response);
        auto* storage = typed_response.mutable_storage();
        storage->mutable_metadata()->set_transaction_uuid(typed_request.metadata().transaction_uuid());
        storage->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        atfw::distributed_system::transaction_participator_failure_reason sample_data;
        ATFW_EXPLICIT_UNUSED_ATTR bool packed = storage->mutable_transaction_data()->PackFrom(sample_data);
        RPC_RETURN_CODE(0);
      },
      node3_options);
  CASE_EXPECT_TRUE(!!error_rule && !!node2_rule && !!node3_rule);

  auto task = test.run_task("api_query_replication_merge", std::chrono::seconds{6},
                            [&replication_metadata](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage output;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::query_transaction(ctx, replication_metadata, output));
    CASE_EXPECT_EQ(0, res);
    // Merged replica fields: the later terminal wins, node2's finish timepoint is adopted and
    // node3's transaction data lands in the same output.
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
        output.metadata().status());
    CASE_EXPECT_EQ(123, output.metadata().finish_timepoint().seconds());
    CASE_EXPECT_EQ(1, output.participators().size());
    CASE_EXPECT_TRUE(output.has_transaction_data());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(3, test.ss().calls(rpc::transaction::packer::get_full_name_of_query()));

  // Conflicting replica terminals (REJECTED vs COMMITED) still merge deterministically to the
  // later one (COMMITED) without returning an error.
  node2_rule.reset();
  atfw::testing::ss_rule_options node2_rejected_options;
  node2_rejected_options.match_node_id = 0x1B0002;
  auto node2_rejected_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      SSDistributeTransactionQueryReq::descriptor()->full_name(),
      SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionQueryRsp&>(response);
        auto* storage = typed_response.mutable_storage();
        storage->mutable_metadata()->set_transaction_uuid(typed_request.metadata().transaction_uuid());
        storage->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        storage->mutable_metadata()->mutable_finish_timepoint()->set_seconds(456);
        RPC_RETURN_CODE(0);
      },
      node2_rejected_options);
  CASE_EXPECT_TRUE(!!node2_rejected_rule);

  auto conflict_task = test.run_task("api_query_replication_conflict", std::chrono::seconds{6},
                                     [&replication_metadata](rpc::context& ctx) -> rpc::result_code_type {
    transaction_blob_storage output;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::query_transaction(ctx, replication_metadata, output));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
        output.metadata().status());
    CASE_EXPECT_EQ(456, output.metadata().finish_timepoint().seconds());
    RPC_RETURN_CODE(0);
  });
  auto conflict_result = test.wait(conflict_task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(conflict_result.task_exited);
  CASE_EXPECT_EQ(0, conflict_result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ participator terminal RPC error paths (4.1 rows) ============

CASE_TEST(component_distributed_transaction_api, participator_terminal_error_paths) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  transaction_metadata metadata;
  metadata.set_transaction_uuid("api-participator-errors-1");
  metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);

  // notfound propagates as-is for both directions.
  auto notfound_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_commit_participator(),
                                            PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  CASE_EXPECT_TRUE(!!notfound_rule);
  auto task = test.run_task("api_participator_notfound", std::chrono::seconds{4},
                            [&metadata](rpc::context& ctx) -> rpc::result_code_type {
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_participator(ctx, "pa", metadata)));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // OLD_VERSION is a single call for both participant directions (no internal retry).
  notfound_rule.reset();
  auto commit_conflict = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_commit_participator(),
                                              PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
  auto reject_conflict = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_reject_participator(),
                                              PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
  CASE_EXPECT_TRUE(!!commit_conflict && !!reject_conflict);
  auto conflict_task = test.run_task("api_participator_conflicts", std::chrono::seconds{4},
                                     [&metadata](rpc::context& ctx) -> rpc::result_code_type {
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_participator(ctx, "pa", metadata)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::reject_participator(ctx, "pa", metadata)));
    RPC_RETURN_CODE(0);
  });
  auto conflict_result = test.wait(conflict_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(conflict_result.task_exited);
  CASE_EXPECT_EQ(0, conflict_result.result_code);
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit_participator()));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject_participator()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ remove_no_wait: replication fans out to every replica (4.1 row) ============

CASE_TEST(component_distributed_transaction_api, remove_no_wait_replication_fanout) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002, 0x1B0003}));

  auto task = test.run_task("api_remove_nowait_replication", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-remove-nowait-replication-1");
    metadata.set_replicate_read_count(2);
    metadata.add_replicate_node_server_id(0x1B0001);
    metadata.add_replicate_node_server_id(0x1B0002);
    metadata.add_replicate_node_server_id(0x1B0003);
    // Unmatched one-way sends are recorded and dropped with success.
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction_no_wait(ctx, metadata)));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  // The one-way remove is sent to every replica (N=3), not only R=2 of them.
  CASE_EXPECT_EQ(3, test.ss().calls(rpc::transaction::packer::get_full_name_of_remove()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ commit reports the persisted terminal, never the requested direction ============

CASE_TEST(component_distributed_transaction_api, commit_reports_persisted_terminal) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // The coordinator already decided REJECTED: the commit response carries the persisted terminal
  // and the merged inout must not look commit-confirmed to the caller.
  auto rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      SSDistributeTransactionCommitReq::descriptor()->full_name(),
      SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionCommitReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionCommitRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);

  auto task = test.run_task("api_commit_persisted_terminal", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-commit-persisted-1");
    metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::commit_transaction(ctx, metadata));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED, metadata.status());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 5.1.6: full R x alive-replica matrix counts only valid successes ============

CASE_TEST(component_distributed_transaction_api, replication_quorum_full_matrix) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002, 0x1B0003}));

  auto register_node_rule = [&test](uint64_t node_id, bool alive) {
    atfw::testing::ss_rule_options node_options;
    node_options.match_node_id = node_id;
    if (alive) {
      return test.ss().mock(
          rpc::transaction::packer::get_full_name_of_create(),
          SSDistributeTransactionCreateReq::descriptor()->full_name(),
          SSDistributeTransactionCreateRsp::descriptor()->full_name(),
          [](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
            RPC_RETURN_CODE(0);
          },
          node_options);
    }
    return test.ss().mock_error(rpc::transaction::packer::get_full_name_of_create(),
                                PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, node_options);
  };

  // Every (R, alive) combination: the create succeeds exactly when alive >= R.
  for (uint32_t replication_read_count = 1; replication_read_count <= 3; ++replication_read_count) {
    for (uint32_t alive_count = 1; alive_count <= 3; ++alive_count) {
      atfw::testing::ss_rule_handle rules[3] = {register_node_rule(0x1B0001, alive_count > 0),
                                                register_node_rule(0x1B0002, alive_count > 1),
                                                register_node_rule(0x1B0003, alive_count > 2)};
      CASE_EXPECT_TRUE(!!rules[0] && !!rules[1] && !!rules[2]);

      auto task = test.run_task("quorum_matrix", std::chrono::seconds{6},
                                [replication_read_count, alive_count](rpc::context& ctx) -> rpc::result_code_type {
        transaction_blob_storage storage;
        storage.mutable_metadata()->set_transaction_uuid("api-quorum-matrix");
        storage.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        storage.mutable_metadata()->set_replicate_read_count(replication_read_count);
        storage.mutable_metadata()->add_replicate_node_server_id(0x1B0001);
        storage.mutable_metadata()->add_replicate_node_server_id(0x1B0002);
        storage.mutable_metadata()->add_replicate_node_server_id(0x1B0003);
        storage.mutable_configure()->set_resolve_max_times(3);
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::create_transaction(ctx, storage));
        if (alive_count >= replication_read_count) {
          CASE_EXPECT_EQ(0, res);
        } else {
          CASE_EXPECT_TRUE(res < 0);
        }
        RPC_RETURN_CODE(0);
      });
      auto result = test.wait(task, std::chrono::seconds{12});
      CASE_EXPECT_TRUE(result.task_exited);
      CASE_EXPECT_EQ(0, result.result_code);

      rules[0].reset();
      rules[1].reset();
      rules[2].reset();
    }
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 5.1.5: the SDK falls back to a surviving replica id ============

CASE_TEST(component_distributed_transaction_api, calculate_server_id_prefers_alive_replica) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  // Only the second replica exists in discovery: the first preferred id is dead after a scale-down.
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0002}));

  uint64_t seen_target = 0;
  auto rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      SSDistributeTransactionQueryReq::descriptor()->full_name(),
      SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [&seen_target](const atfw::testing::ss_request_view& request,
                     google::protobuf::Message& response) -> rpc::result_code_type {
        seen_target = request.target_node_id;
        const auto& typed_request = static_cast<const SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionQueryRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_storage()->mutable_metadata(), typed_request.metadata());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!rule);

  auto task = test.run_task("failover_replica", std::chrono::seconds{4},
      [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-failover-1");
    // Non-replication routing still consults the surviving ids of the metadata first.
    metadata.add_replicate_node_server_id(0x1B0001);  // dead: not in discovery
    metadata.add_replicate_node_server_id(0x1B0002);  // alive
    transaction_blob_storage output;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction_api::query_transaction(ctx, metadata, output));
    CASE_EXPECT_EQ(0, res);
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(static_cast<uint64_t>(0x1B0002), seen_target);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ §4.5 补充：remove_no_wait 按 N 个副本扇出，发送失败按 R 阈值判定 ============
CASE_TEST(component_distributed_transaction_api, remove_no_wait_send_failure_threshold) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  // 发现中只注入两个节点；元数据列出的第三个节点（0x1B0BAD）路由不到，
  // router 在消息到达 mock SS 引擎之前即以 EN_ATBUS_ERR_ATNODE_INVALID_ID 失败
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002}));

  // Leg 1: R=2，3 个副本中 1 个发送失败 —— 成功数 2 达到阈值，整体返回 0
  auto task_ok = test.run_task("remove_nowait_threshold_ok", std::chrono::seconds{4},
                               [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-remove-nowait-threshold-ok");
    metadata.set_replicate_read_count(2);
    metadata.add_replicate_node_server_id(0x1B0001);
    metadata.add_replicate_node_server_id(0x1B0002);
    metadata.add_replicate_node_server_id(0x1B0BAD);  // 未知节点：发送立即失败
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction_no_wait(ctx, metadata)));
    RPC_RETURN_CODE(0);
  });
  auto result_ok = test.wait(task_ok, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result_ok.task_exited);
  CASE_EXPECT_EQ(0, result_ok.result_code);
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  // 失败的发送从未到达 SS 引擎：只有两个存活节点产生了引擎侧调用
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_remove()));

  // Leg 2: R=3，同样的 1 个发送失败使成功数 2 < 3 —— 返回最近一次失败错误码
  auto task_fail = test.run_task("remove_nowait_threshold_fail", std::chrono::seconds{4},
                                 [](rpc::context& ctx) -> rpc::result_code_type {
    transaction_metadata metadata;
    metadata.set_transaction_uuid("api-remove-nowait-threshold-fail");
    metadata.set_replicate_read_count(3);
    metadata.add_replicate_node_server_id(0x1B0001);
    metadata.add_replicate_node_server_id(0x1B0002);
    metadata.add_replicate_node_server_id(0x1B0BAD);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_ATBUS_ERR_ATNODE_INVALID_ID,
                   RPC_AWAIT_CODE_RESULT(rpc::transaction_api::remove_transaction_no_wait(ctx, metadata)));
    RPC_RETURN_CODE(0);
  });
  auto result_fail = test.wait(task_fail, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result_fail.task_exited);
  CASE_EXPECT_EQ(0, result_fail.result_code);
  for (int i = 0; i < 4; ++i) {
    test.pump_once();
  }
  CASE_EXPECT_EQ(4, test.ss().calls(rpc::transaction::packer::get_full_name_of_remove()));

  CASE_EXPECT_EQ(0, test.stop());
}
