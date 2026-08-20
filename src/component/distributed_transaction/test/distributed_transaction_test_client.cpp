// Copyright 2026 atframework
//
// transaction_client_handle unit tests (UNIT_TEST_EXECUTION_PLAN.md section 4.2): construction and
// layout compatibility (DT-024), create/set/add mutations (DT-010), and the submit state machine —
// normal 2PC with an ordered event list, coordinator-terminal-only decisions (DT-006), bounded
// retries (DT-002), separated delivery results (DT-014) and force_commit (DT-023).

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

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <memory/object_allocator.h>  // NOLINT(build/include_order)

#include "dt_test_common.h"  // NOLINT(build/include_subdir)
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "utility/protobuf_mini_dumper.h"
#include "transaction_client_handle.h"  // NOLINT(build/include_subdir)

namespace {
using atframework::distributed_system::EnDistibutedTransactionStatus;
using atframework::distributed_system::SSDistributeTransactionCommitReq;
using atframework::distributed_system::SSDistributeTransactionCommitRsp;
using atframework::distributed_system::SSDistributeTransactionCreateReq;
using atfw::distributed_system::SSDistributeTransactionCreateRsp;
using atframework::distributed_system::SSDistributeTransactionRejectReq;
using atframework::distributed_system::SSDistributeTransactionRejectRsp;
using atframework::distributed_system::transaction_client_handle;
using atframework::distributed_system::transaction_participator_failure_reason;
using sample_data_type = atfw::distributed_system::transaction_participator_failure_reason;

// Records every vtable call as an ordered event list; counting only would hide ordering bugs.
struct client_event_recorder {
  std::vector<std::string> events;

  // Per-participator behavior scripts: participator key -> vector of return codes consumed FIFO;
  // an empty script means "always succeed".
  std::unordered_map<std::string, std::vector<int32_t>> prepare_scripts;
  std::unordered_map<std::string, std::vector<int32_t>> commit_scripts;
  std::unordered_map<std::string, std::vector<int32_t>> reject_scripts;
  // participator keys whose prepare response sets allow_retry=true
  std::unordered_set<std::string> prepare_allow_retry;
  // Per-participator allow_retry FIFO scripts consumed before falling back to prepare_allow_retry;
  // used to script mixed retry/no-retry sequences across attempts.
  std::unordered_map<std::string, std::vector<int32_t>> prepare_allow_retry_scripts;

  atfw::util::memory::strong_rc_ptr<transaction_client_handle::vtable_type> make_vtable() {
    auto vtable = atfw::component::memory::stl::make_strong_rc<transaction_client_handle::vtable_type>();
    vtable->prepare_participator = [this](rpc::context&, transaction_client_handle&,
                                          const transaction_client_handle::storage_type&,
                                          const transaction_client_handle::participator_type& participator,
                                          transaction_participator_failure_reason& reason) -> rpc::result_code_type {
      events.push_back("prepare:" + participator.participator_key());
      auto& script = prepare_scripts[participator.participator_key()];
      int32_t res = 0;
      if (!script.empty()) {
        res = script.front();
        script.erase(script.begin());
      }
      auto& allow_retry_script = prepare_allow_retry_scripts[participator.participator_key()];
      if (!allow_retry_script.empty()) {
        if (allow_retry_script.front() != 0) {
          reason.set_allow_retry(true);
        }
        allow_retry_script.erase(allow_retry_script.begin());
      } else if (prepare_allow_retry.count(participator.participator_key()) > 0) {
        reason.set_allow_retry(true);
      }
      RPC_RETURN_CODE(res);
    };
    vtable->commit_participator = [this](rpc::context&, transaction_client_handle&,
                                         const transaction_client_handle::storage_type&,
                                         const transaction_client_handle::participator_type& participator)
        -> rpc::result_code_type {
      events.push_back("commit:" + participator.participator_key());
      auto& script = commit_scripts[participator.participator_key()];
      int32_t res = 0;
      if (!script.empty()) {
        res = script.front();
        script.erase(script.begin());
      }
      RPC_RETURN_CODE(res);
    };
    vtable->reject_participator = [this](rpc::context&, transaction_client_handle&,
                                         const transaction_client_handle::storage_type&,
                                         const transaction_client_handle::participator_type& participator)
        -> rpc::result_code_type {
      events.push_back("reject:" + participator.participator_key());
      auto& script = reject_scripts[participator.participator_key()];
      int32_t res = 0;
      if (!script.empty()) {
        res = script.front();
        script.erase(script.begin());
      }
      RPC_RETURN_CODE(res);
    };
    return vtable;
  }
};

// Registers coordinator mocks that accept create and answer commit/reject with the persisted
// terminal metadata. Terminal can be switched between COMMITED and REJECTED by the handler state.
struct coordinator_mock_state {
  EnDistibutedTransactionStatus terminal = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED;
};

atfw::testing::ss_rule_handle register_coordinator_commit_mock(atfw::testing::runtime& test,
                                                               coordinator_mock_state& state) {
  return test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit(),
      SSDistributeTransactionCommitReq::descriptor()->full_name(),
      SSDistributeTransactionCommitRsp::descriptor()->full_name(),
      [&state](const atfw::testing::ss_request_view& request,
               google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionCommitReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionCommitRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        typed_response.mutable_metadata()->set_status(state.terminal);
        RPC_RETURN_CODE(0);
      });
}

atfw::testing::ss_rule_handle register_coordinator_reject_mock(atfw::testing::runtime& test,
                                                               coordinator_mock_state& state) {
  return test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject(),
      SSDistributeTransactionRejectReq::descriptor()->full_name(),
      SSDistributeTransactionRejectRsp::descriptor()->full_name(),
      [&state](const atfw::testing::ss_request_view& request,
               google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request = static_cast<const SSDistributeTransactionRejectReq&>(request.body);
        auto& typed_response = static_cast<SSDistributeTransactionRejectRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        // reject keeps the coordinator truth: if the coordinator already decided COMMITED, the
        // response says COMMITED and the client must not deliver reject notifications.
        typed_response.mutable_metadata()->set_status(state.terminal);
        RPC_RETURN_CODE(0);
      });
}

atfw::testing::ss_rule_handle register_coordinator_create_mock(atfw::testing::runtime& test,
    int* call_count = nullptr) {
  return test.ss().mock(
      rpc::transaction::packer::get_full_name_of_create(),
      SSDistributeTransactionCreateReq::descriptor()->full_name(),
      SSDistributeTransactionCreateRsp::descriptor()->full_name(),
      [call_count](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        if (nullptr != call_count) {
          ++*call_count;
        }
        RPC_RETURN_CODE(0);
      });
}
}  // namespace

// ============ DT-024: construction, layout and destroy callback ============

CASE_TEST(component_distributed_transaction_client, legacy_constructor_and_layout_compatibility_dt024) {
  static int destroy_count = 0;
  destroy_count = 0;

  // Stack object with a null vtable is directly constructible.
  {
    transaction_client_handle stack_client{nullptr};
    CASE_EXPECT_EQ(nullptr, stack_client.get_private_data());
    stack_client.set_private_data(&stack_client);
    CASE_EXPECT_EQ(&stack_client, stack_client.get_private_data());

    auto callback = [](transaction_client_handle*) { ++destroy_count; };
    stack_client.set_on_destroy_callback(callback);
    CASE_EXPECT_TRUE(nullptr != stack_client.get_on_destroy_callback());
  }
  // Exactly one destroy callback for the stack object.
  CASE_EXPECT_EQ(1, destroy_count);

  // strong_rc_ptr holding with a real vtable.
  {
    client_event_recorder recorder;
    auto vtable = recorder.make_vtable();
    auto shared_client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);
    CASE_EXPECT_TRUE(!!shared_client);
    auto callback = [](transaction_client_handle*) { ++destroy_count; };
    shared_client->set_on_destroy_callback(callback);
    shared_client.reset();
  }
  CASE_EXPECT_EQ(2, destroy_count);
}

// ============ create_transaction option mapping ============

CASE_TEST(component_distributed_transaction_client, create_transaction_options_mapping) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_create_default", std::chrono::seconds{4},
                            [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_TRUE(!!storage);
    CASE_EXPECT_FALSE(storage->metadata().transaction_uuid().empty());
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED,
                   storage->metadata().status());
    // default options map exactly
    CASE_EXPECT_EQ(0, storage->metadata().replicate_read_count());
    CASE_EXPECT_EQ(0, storage->metadata().replicate_node_server_id_size());
    CASE_EXPECT_FALSE(storage->metadata().memory_only());
    CASE_EXPECT_FALSE(storage->configure().force_commit());
    CASE_EXPECT_EQ(3, storage->configure().resolve_max_times());
    CASE_EXPECT_EQ(5, storage->configure().lock_retry_max_times());
    CASE_EXPECT_EQ(std::chrono::seconds{10},
                   protobuf_to_chrono_duration(storage->configure().resolve_retry_interval()));
    CASE_EXPECT_EQ(std::chrono::milliseconds{32},
                   protobuf_to_chrono_duration(storage->configure().lock_wait_interval_min()));
    CASE_EXPECT_EQ(std::chrono::milliseconds{256},
                   protobuf_to_chrono_duration(storage->configure().lock_wait_interval_max()));
    auto expire = protobuf_to_system_clock(storage->metadata().expire_timepoint());
    auto prepare = protobuf_to_system_clock(storage->metadata().prepare_timepoint());
    CASE_EXPECT_EQ(std::chrono::seconds{5}, expire - prepare);

    // custom options map exactly and replace a previous output pointer
    transaction_client_handle::transaction_options custom_options;
    custom_options.replication_read_count = 0;
    custom_options.replication_total_count = 0;
    custom_options.memory_only = true;
    custom_options.force_commit = true;
    custom_options.timeout = std::chrono::milliseconds{1500};
    custom_options.resolve_max_times = 7;
    custom_options.lock_retry_max_times = 9;
    custom_options.resolve_retry_interval = std::chrono::milliseconds{11};
    custom_options.lock_wait_interval_min = std::chrono::milliseconds{3};
    custom_options.lock_wait_interval_max = std::chrono::milliseconds{4};
    transaction_client_handle::storage_ptr_type custom_storage = storage;
    res = RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, custom_storage, custom_options));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_TRUE(!!custom_storage);
    CASE_EXPECT_NE(storage.get(), custom_storage.get());  // the old pointer was replaced
    CASE_EXPECT_TRUE(custom_storage->metadata().memory_only());
    CASE_EXPECT_TRUE(custom_storage->configure().force_commit());
    CASE_EXPECT_EQ(7, custom_storage->configure().resolve_max_times());
    CASE_EXPECT_EQ(9, custom_storage->configure().lock_retry_max_times());
    CASE_EXPECT_EQ(std::chrono::milliseconds{11},
                   protobuf_to_chrono_duration(custom_storage->configure().resolve_retry_interval()));
    CASE_EXPECT_EQ(std::chrono::milliseconds{3},
                   protobuf_to_chrono_duration(custom_storage->configure().lock_wait_interval_min()));
    CASE_EXPECT_EQ(std::chrono::milliseconds{4},
                   protobuf_to_chrono_duration(custom_storage->configure().lock_wait_interval_max()));
    CASE_EXPECT_NE(custom_storage->metadata().transaction_uuid(), storage->metadata().transaction_uuid());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ set_transaction_data / add_participator (DT-010) ============

CASE_TEST(component_distributed_transaction_client, set_data_and_add_participator_dt010) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_mutations", std::chrono::seconds{4},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    sample_data_type sample_data;
    sample_data.set_allow_retry(true);

    // null input
    transaction_client_handle::storage_ptr_type null_storage;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, client->set_transaction_data(ctx, null_storage,
        sample_data));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   client->add_participator(ctx, null_storage, "pa", sample_data));

    transaction_client_handle::storage_ptr_type storage;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage)));
    CASE_EXPECT_EQ(0, client->set_transaction_data(ctx, storage, sample_data));
    CASE_EXPECT_TRUE(storage->transaction_data().Is<sample_data_type>());

    // add_participator with an empty key still stores under the empty map key
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "", sample_data));
    CASE_EXPECT_EQ(1, storage->participators().size());

    // first add
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(2, storage->participators().size());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_EQ("pa", (*storage->mutable_participators())["pa"].participator_key());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_TRUE((*storage->mutable_participators())["pa"].has_participator_data());

    // DT-010: a duplicate key is a successful update that replaces the content
    sample_data_type updated_data;
    updated_data.set_allow_retry(false);
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", updated_data));
    CASE_EXPECT_EQ(2, storage->participators().size());
    sample_data_type unpacked;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    CASE_EXPECT_TRUE((*storage->mutable_participators())["pa"].participator_data().UnpackTo(&unpacked));
    CASE_EXPECT_FALSE(unpacked.allow_retry());

    // PREPARED and later states reject mutations
    storage->mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN,
                   client->set_transaction_data(ctx, storage, sample_data));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN,
                   client->add_participator(ctx, storage, "pb", sample_data));
    CASE_EXPECT_EQ(2, storage->participators().size());  // no partial insertion leaked
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ submit: normal 2PC happy path with an ordered event list ============

CASE_TEST(component_distributed_transaction_client, submit_happy_path_ordered_events) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  coordinator_mock_state coord_state;
  auto create_rule = register_coordinator_create_mock(test);
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule && !!reject_rule);

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_submit_happy", std::chrono::seconds{6},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    sample_data.set_allow_retry(true);
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pb", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage->metadata().status());
    CASE_EXPECT_EQ(2, prepared.size());
    CASE_EXPECT_TRUE(failed.empty());

    // submit twice is rejected (the transaction already ran)
    res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_ALREADY_RUN, res);
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // All participants prepare before any terminal notification is delivered (protobuf map
  // iteration order between participants is not stable, so the per-phase ordering is asserted).
  CASE_EXPECT_TRUE(dt_test::expect_event_multiset(recorder.events,
                                                  {"prepare:pa", "prepare:pb", "commit:pa", "commit:pb"}));
  CASE_EXPECT_TRUE(dt_test::expect_all_before(recorder.events, "prepare:", "commit:"));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-006: the coordinator terminal is the only decision ============

CASE_TEST(component_distributed_transaction_client, coordinator_terminal_is_only_decision_dt006) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // Phase 1: prepare fails on the second participant. The client must persist the coordinator
  // REJECTED *before* notifying any participant, and only prepared participants get reject
  // notifications.
  coordinator_mock_state coord_state;
  coord_state.terminal = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  auto create_rule = register_coordinator_create_mock(test);
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule && !!reject_rule);

  // A single participant keeps the failure point deterministic (map iteration order between
  // participants is not stable).
  client_event_recorder recorder;
  recorder.prepare_scripts["pa"] = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_prepare_failure", std::chrono::seconds{6},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   storage->metadata().status());
    CASE_EXPECT_TRUE(prepared.empty());
    // The prepare-failed participant lands in output_failed_participators.
    CASE_EXPECT_EQ(1, failed.size());
    CASE_EXPECT_EQ(1, failed.count("pa"));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // prepare(pa) failed with a definitive RPC error (delivery uncertain): the coordinator REJECTED
  // decision is persisted first, then pa receives exactly one undo notification.
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"prepare:pa", "reject:pa"}));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()));
  // The reject decision was persisted on the coordinator before notifying pa.
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));

  // Phase 2: the coordinator already decided COMMITED but the client only sees a commit error (the
  // decision may or may not have persisted — the client cannot know from an error). It must
  // converge to the persisted terminal via a bounded query and never send reject notifications.
  recorder.events.clear();
  commit_rule.reset();
  auto lost_commit_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_commit(),
                                               PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  CASE_EXPECT_TRUE(!!lost_commit_rule);
  auto query_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      atfw::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [](const atfw::testing::ss_request_view& request,
         google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<atfw::distributed_system::SSDistributeTransactionQueryRsp&>(response);
        auto* query_storage = typed_response.mutable_storage();
        protobuf_copy_message(*query_storage->mutable_metadata(), typed_request.metadata());
        // The coordinator persisted COMMITED although the response never reached the client.
        query_storage->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!query_rule);

  auto task2 = test.run_task("client_commit_response_lost", std::chrono::seconds{8},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pb", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    // The coordinator terminal (COMMITED via the bounded query) decides the result.
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage->metadata().status());
    CASE_EXPECT_TRUE(failed.empty());
    RPC_RETURN_CODE(0);
  });
  auto result2 = test.wait(task2, std::chrono::seconds{16});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);

  // Notifications follow the persisted direction only: commit for both, never reject.
  bool has_reject = false;
  for (const auto& event : recorder.events) {
    if (event.rfind("reject:", 0) == 0) {
      has_reject = true;
    }
  }
  CASE_EXPECT_FALSE(has_reject);
  CASE_EXPECT_TRUE(dt_test::expect_event_multiset(recorder.events,
                                                  {"prepare:pa", "prepare:pb", "commit:pa", "commit:pb"}));
  CASE_EXPECT_TRUE(dt_test::expect_all_before(recorder.events, "prepare:", "commit:"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-002: retry exhaustion never fakes commit ============

CASE_TEST(component_distributed_transaction_client, retry_exhaustion_never_fakes_commit_dt002) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // Path A: the transaction is already expired at submit time — no create, no prepare, no commit.
  coordinator_mock_state coord_state;
  coord_state.terminal = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  int create_calls = 0;
  auto create_rule = register_coordinator_create_mock(test, &create_calls);
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule && !!reject_rule);

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_expired_entry", std::chrono::seconds{6},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));
    // Force an already-expired transaction.
    storage->mutable_metadata()->mutable_expire_timepoint()->set_seconds(1);
    storage->mutable_metadata()->mutable_expire_timepoint()->set_nanos(0);

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, res);
    CASE_EXPECT_TRUE(prepared.empty());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, create_calls);  // expired-at-entry: no coordinator create
  CASE_EXPECT_TRUE(recorder.events.empty());
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));

  // Path B: prepare keeps answering 0 + allow_retry forever. Retries are bounded, the coordinator
  // persists REJECTED and the submit never reports success.
  recorder.events.clear();
  recorder.prepare_allow_retry.insert("pa");
  auto task2 = test.run_task("client_allow_retry_exhaustion", std::chrono::seconds{8},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    client_options.lock_retry_max_times = 3;
    client_options.lock_wait_interval_min = std::chrono::milliseconds{1};
    client_options.lock_wait_interval_max = std::chrono::milliseconds{2};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_TRUE(res < 0);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   storage->metadata().status());
    CASE_EXPECT_TRUE(prepared.empty());  // exhausting retries must not count as success
    RPC_RETURN_CODE(0);
  });
  auto result2 = test.wait(task2, std::chrono::seconds{16});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);

  // lock_retry_max_times=3 -> 4 prepare rounds, then the coordinator reject; zero commit
  // notifications anywhere.
  CASE_EXPECT_EQ(4, recorder.events.size());
  for (const auto& event : recorder.events) {
    CASE_EXPECT_EQ(std::string("prepare:pa"), event);
  }
  CASE_EXPECT_EQ(1, create_calls);  // path B created the coordinator record
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-014: delivery failures are separated from the transaction result ============

CASE_TEST(component_distributed_transaction_client, terminal_delivery_result_is_separate_and_bounded_dt014) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  coordinator_mock_state coord_state;
  auto create_rule = register_coordinator_create_mock(test);
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule && !!reject_rule);

  // pa: always fails (bounded to resolve_max_times); pb: fails once then succeeds. Sequential
  // delivery keeps both participants notified in order.
  client_event_recorder recorder;
  recorder.commit_scripts["pa"] = {-1, -1, -1, -1, -1, -1, -1, -1};
  recorder.commit_scripts["pb"] = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_delivery_failure", std::chrono::seconds{8},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    client_options.resolve_max_times = 3;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pb", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    // The transaction result comes from the coordinator terminal, not the last notification code.
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage->metadata().status());
    // Delivery failures do NOT enter output_failed_participators (the participant resolve flow
    // owns final consistency for them); only prepare failures do.
    CASE_EXPECT_TRUE(failed.empty());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{16});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // pa tried exactly 3 times (resolve_max_times), pb succeeded on its second attempt; delivery is
  // sequential per participant and every prepare happens before any commit notification.
  CASE_EXPECT_TRUE(dt_test::expect_event_multiset(recorder.events,
                                                  {"prepare:pa", "prepare:pb", "commit:pa", "commit:pa",
                                                   "commit:pa", "commit:pb", "commit:pb"}));
  CASE_EXPECT_TRUE(dt_test::expect_all_before(recorder.events, "prepare:", "commit:"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-023: force_commit does not require the commit callback ============

CASE_TEST(component_distributed_transaction_client, force_commit_does_not_require_commit_callback_dt023) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int create_calls = 0;
  auto create_rule = register_coordinator_create_mock(test, &create_calls);

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  // force_commit success path completes inside prepare; the commit callback is never needed. The
  // reject callback stays for the compensation path.
  vtable->commit_participator = nullptr;
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_force_commit", std::chrono::seconds{6},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.force_commit = true;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pb", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage->metadata().status());
    CASE_EXPECT_EQ(2, prepared.size());
    CASE_EXPECT_TRUE(failed.empty());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  // force_commit never creates a coordinator record and never notifies commit.
  CASE_EXPECT_EQ(0, create_calls);
  CASE_EXPECT_TRUE(dt_test::expect_event_multiset(recorder.events, {"prepare:pa", "prepare:pb"}));

  // Failure path: force_commit prepare fails -> bounded undo via reject_participator.
  recorder.events.clear();
  recorder.prepare_scripts["pa"] = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto task2 = test.run_task("client_force_commit_failure", std::chrono::seconds{6},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.force_commit = true;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    CASE_EXPECT_TRUE(res < 0);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   storage->metadata().status());
    CASE_EXPECT_EQ(1, failed.count("pa"));
    RPC_RETURN_CODE(0);
  });
  auto result2 = test.wait(task2, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result2.task_exited);
  CASE_EXPECT_EQ(0, result2.result_code);
  // pa failed its prepare with a delivery-uncertain error: it receives exactly one undo
  // notification within the force_commit compensation budget.
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"prepare:pa", "reject:pa"}));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ submit parameter guards + coordinator CAS retry budget ============

CASE_TEST(component_distributed_transaction_client, submit_guards_and_create_cas_retry) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // The first four creates conflict (stale replica CAS), the fifth succeeds: the client retry
  // budget is 5 attempts.
  atfw::testing::ss_rule_options conflict_options;
  conflict_options.times = 4;
  auto conflict_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_create(),
                                            PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, conflict_options);
  auto create_rule = register_coordinator_create_mock(test);
  CASE_EXPECT_TRUE(!!conflict_rule && !!create_rule);

  coordinator_mock_state coord_state;
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!commit_rule && !!reject_rule);

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_guards", std::chrono::seconds{6}, [&client,
      &recorder](rpc::context& ctx) -> rpc::result_code_type {
    sample_data_type sample_data;

    // null input
    transaction_client_handle::storage_ptr_type null_storage;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, null_storage)));

    // missing vtable callbacks
    auto empty_vtable = atfw::component::memory::stl::make_strong_rc<transaction_client_handle::vtable_type>();
    auto vtable_less_client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(empty_vtable);
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(vtable_less_client->submit_transaction(ctx, storage)));

    // no participators
    transaction_client_handle::storage_ptr_type empty_storage;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, empty_storage, client_options)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, empty_storage)));

    // normal mode without a commit callback is rejected at the entry
    auto vtable_no_commit = recorder.make_vtable();
    vtable_no_commit->commit_participator = nullptr;
    auto no_commit_client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable_no_commit);
    transaction_client_handle::storage_ptr_type no_commit_storage;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(no_commit_client->create_transaction(ctx, no_commit_storage,
        client_options)));
    CASE_EXPECT_EQ(0, no_commit_client->add_participator(ctx, no_commit_storage, "pa", sample_data));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(no_commit_client->submit_transaction(ctx, no_commit_storage)));

    // happy path with the CAS-retry create
    transaction_client_handle::storage_ptr_type ok_storage;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, ok_storage, client_options)));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, ok_storage, "pa", sample_data));
    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, ok_storage, &prepared, &failed));
    CASE_EXPECT_EQ(0, res);
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // 4 conflicts + 1 success = 5 create calls, exactly the bounded retry budget.
  CASE_EXPECT_EQ(5, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()));
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"prepare:pa", "commit:pa"}));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ create failure rolls the status back (4.2 submit row) ============

CASE_TEST(component_distributed_transaction_client, create_failure_rolls_back_status) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int create_calls = 0;
  auto create_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_create(),
                                          PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  CASE_EXPECT_TRUE(!!create_rule);
  create_rule.reset();
  auto counting_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_create(),
      SSDistributeTransactionCreateReq::descriptor()->full_name(),
      SSDistributeTransactionCreateRsp::descriptor()->full_name(),
      [&create_calls](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        ++create_calls;
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
      });
  CASE_EXPECT_TRUE(!!counting_rule);

  client_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_create_failure", std::chrono::seconds{6},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    // A generic create error (not OLD_VERSION/KEY_EXISTS) is not retried.
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, res);
    // The PREPARED status set at submit entry is rolled back so the storage stays reusable.
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED,
        storage->metadata().status());
    CASE_EXPECT_TRUE(prepared.empty());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{12});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, create_calls);  // generic errors break the retry loop immediately
  CASE_EXPECT_TRUE(recorder.events.empty());  // no participant was ever contacted

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-014 symmetric: REJECTED decision with failing reject notifications ============

CASE_TEST(component_distributed_transaction_client, rejected_delivery_failure_is_bounded_dt014_symmetric) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // The coordinator answers commit with the already-persisted REJECTED terminal: the client must
  // follow that direction and only send reject notifications.
  coordinator_mock_state coord_state;
  coord_state.terminal = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  auto create_rule = register_coordinator_create_mock(test);
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule && !!reject_rule);

  client_event_recorder recorder;
  recorder.reject_scripts["pa"] = {-1, -1, -1, -1, -1, -1, -1, -1};
  recorder.reject_scripts["pb"] = {-1, -1, -1, -1, -1, -1, -1, -1};
  auto vtable = recorder.make_vtable();
  auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);

  auto task = test.run_task("client_rejected_delivery_failure", std::chrono::seconds{8},
      [&client](rpc::context& ctx) -> rpc::result_code_type {
    transaction_client_handle::storage_ptr_type storage;
    transaction_client_handle::transaction_options client_options;
    client_options.resolve_retry_interval = std::chrono::milliseconds{10};
    client_options.resolve_max_times = 3;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));

    sample_data_type sample_data;
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));
    CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pb", sample_data));

    std::unordered_set<std::string> prepared;
    std::unordered_set<std::string> failed;
    int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
    // The coordinator rejected although every participant prepared.
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED, res);
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   storage->metadata().status());
    CASE_EXPECT_EQ(2, prepared.size());
    // Delivery failures never enter output_failed_participators.
    CASE_EXPECT_TRUE(failed.empty());
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{16});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Both participants were notified in the reject direction exactly 3 times (resolve_max_times).
  CASE_EXPECT_TRUE(dt_test::expect_event_multiset(recorder.events,
                                                  {"prepare:pa", "prepare:pb", "reject:pa", "reject:pa", "reject:pa",
                                                   "reject:pb", "reject:pb", "reject:pb"}));
  CASE_EXPECT_TRUE(dt_test::expect_all_before(recorder.events, "prepare:", "reject:"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ §4.2 补充：create CAS 冲突重试有界（OLD_VERSION 耗尽），KEY_EXISTS 幂等可重试 ============
CASE_TEST(component_distributed_transaction_client, create_cas_retry_exhaustion_and_key_exists_retriable) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  coordinator_mock_state coord_state;
  int create_calls = 0;
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!commit_rule);

  // --- Phase 1: OLD_VERSION 重试耗尽（预算 5 次）：不伪造成功，storage 回滚到 CREATED ---
  {
    atfw::testing::ss_rule_options conflict_options;
    conflict_options.times = 5;  // 覆盖全部 5 次预算：永不成功
    auto conflict_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_create(),
                                              PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, conflict_options);
    CASE_EXPECT_TRUE(!!conflict_rule);
    // 引擎按注册顺序取首个仍有次数预算的活跃规则：错误规则必须先注册，成功规则随后兜底。
    // 规则句柄是 RAII 的，离开本阶段作用域即失效，不会遮蔽下一阶段注册的规则
    auto create_rule = register_coordinator_create_mock(test, &create_calls);
    CASE_EXPECT_TRUE(!!create_rule);

    client_event_recorder recorder;
    auto vtable = recorder.make_vtable();
    auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);
    auto task = test.run_task("create_cas_exhaustion", std::chrono::seconds{6},
                              [&client](rpc::context& ctx) -> rpc::result_code_type {
      transaction_client_handle::storage_ptr_type storage;
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage)));
      sample_data_type sample_data;
      CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

      std::unordered_set<std::string> prepared;
      std::unordered_set<std::string> failed;
      int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
      // 回滚：storage 回到 CREATED，未进入任何 prepare/notify
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED,
                     storage->metadata().status());
      CASE_EXPECT_TRUE(prepared.empty());
      CASE_EXPECT_TRUE(failed.empty());
      RPC_RETURN_CODE(0);
    });
    auto result = test.wait(task, std::chrono::seconds{12});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_EQ(5, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()));
    CASE_EXPECT_EQ(0, create_calls);  // 没有任何一次进入成功 mock
    CASE_EXPECT_TRUE(recorder.events.empty());
  }

  // --- Phase 2: KEY_EXISTS 幂等冲突可重试：2 次冲突后第 3 次成功，流程正常推进到 COMMITED ---
  {
    atfw::testing::ss_rule_options key_exists_options;
    key_exists_options.times = 2;
    auto key_exists_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_create(),
                                                PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, key_exists_options);
    CASE_EXPECT_TRUE(!!key_exists_rule);
    // 同 Phase 1：错误规则先于成功规则注册
    auto create_rule = register_coordinator_create_mock(test, &create_calls);
    CASE_EXPECT_TRUE(!!create_rule);

    const auto create_calls_before = test.ss().calls(rpc::transaction::packer::get_full_name_of_create());
    client_event_recorder recorder;
    auto vtable = recorder.make_vtable();
    auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);
    auto task = test.run_task("create_key_exists_retry", std::chrono::seconds{6},
                              [&client](rpc::context& ctx) -> rpc::result_code_type {
      transaction_client_handle::storage_ptr_type storage;
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage)));
      sample_data_type sample_data;
      CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

      std::unordered_set<std::string> prepared;
      std::unordered_set<std::string> failed;
      int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
      CASE_EXPECT_EQ(0, res);
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                     storage->metadata().status());
      CASE_EXPECT_EQ(1, prepared.size());
      CASE_EXPECT_TRUE(failed.empty());
      RPC_RETURN_CODE(0);
    });
    auto result = test.wait(task, std::chrono::seconds{12});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_EQ(3, test.ss().calls(rpc::transaction::packer::get_full_name_of_create()) - create_calls_before);
    CASE_EXPECT_EQ(1, create_calls);  // 仅第 3 次（成功）进入正常 mock
    CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"prepare:pa", "commit:pa"}));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ §4.2 补充：PREEMPTED+allow_retry 逐次重试直到成功；耗尽时不对已响应者补发 undo ============
CASE_TEST(component_distributed_transaction_client, prepare_preempted_retry_then_success_and_exhaustion_no_undo) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  coordinator_mock_state coord_state;
  auto create_rule = register_coordinator_create_mock(test);
  auto commit_rule = register_coordinator_commit_mock(test, coord_state);
  auto reject_rule = register_coordinator_reject_mock(test, coord_state);
  CASE_EXPECT_TRUE(!!create_rule && !!commit_rule && !!reject_rule);

  // --- Phase 1: 第 1 次 PREEMPTED+allow_retry，第 2 次成功：重试后正常 commit ---
  {
    client_event_recorder recorder;
    recorder.prepare_scripts["pa"] = {PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED, 0};
    recorder.prepare_allow_retry_scripts["pa"] = {1, 0};
    auto vtable = recorder.make_vtable();
    auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);
    auto task = test.run_task("preempted_retry_success", std::chrono::seconds{6},
                              [&client](rpc::context& ctx) -> rpc::result_code_type {
      transaction_client_handle::storage_ptr_type storage;
      transaction_client_handle::transaction_options client_options;
      client_options.lock_retry_max_times = 3;
      client_options.lock_wait_interval_min = std::chrono::milliseconds{1};
      client_options.lock_wait_interval_max = std::chrono::milliseconds{2};
      client_options.resolve_retry_interval = std::chrono::milliseconds{10};
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));
      sample_data_type sample_data;
      CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

      std::unordered_set<std::string> prepared;
      std::unordered_set<std::string> failed;
      int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
      CASE_EXPECT_EQ(0, res);
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                     storage->metadata().status());
      CASE_EXPECT_EQ(1, prepared.size());
      CASE_EXPECT_TRUE(failed.empty());
      RPC_RETURN_CODE(0);
    });
    auto result = test.wait(task, std::chrono::seconds{12});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"prepare:pa", "prepare:pa", "commit:pa"}));
  }

  // --- Phase 2: 持续 PREEMPTED+allow_retry 直至预算耗尽：协调者持久化 REJECTED，
  //     已响应的 failed_participator 不补发 undo/reject ---
  coord_state.terminal = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  // calls() 是整个 runtime 的累计值：用增量隔离 Phase 1 的 commit
  const size_t reject_calls_before = test.ss().calls(rpc::transaction::packer::get_full_name_of_reject());
  const size_t commit_calls_before = test.ss().calls(rpc::transaction::packer::get_full_name_of_commit());
  {
    client_event_recorder recorder;
    recorder.prepare_scripts["pa"] = {PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                                      PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                                      PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                                      PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED};
    recorder.prepare_allow_retry_scripts["pa"] = {1, 1, 1, 1};
    auto vtable = recorder.make_vtable();
    auto client = atfw::component::memory::stl::make_strong_rc<transaction_client_handle>(vtable);
    auto task = test.run_task("preempted_retry_exhaustion", std::chrono::seconds{6},
                              [&client](rpc::context& ctx) -> rpc::result_code_type {
      transaction_client_handle::storage_ptr_type storage;
      transaction_client_handle::transaction_options client_options;
      client_options.lock_retry_max_times = 3;  // 4 次尝试
      client_options.lock_wait_interval_min = std::chrono::milliseconds{1};
      client_options.lock_wait_interval_max = std::chrono::milliseconds{2};
      client_options.resolve_retry_interval = std::chrono::milliseconds{10};
      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(client->create_transaction(ctx, storage, client_options)));
      sample_data_type sample_data;
      CASE_EXPECT_EQ(0, client->add_participator(ctx, storage, "pa", sample_data));

      std::unordered_set<std::string> prepared;
      std::unordered_set<std::string> failed;
      int32_t res = RPC_AWAIT_CODE_RESULT(client->submit_transaction(ctx, storage, &prepared, &failed));
      // 决策已被协调者持久化：返回真实的 prepare 失败码而非伪造结果
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED, res);
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                     storage->metadata().status());
      CASE_EXPECT_TRUE(prepared.empty());
      CASE_EXPECT_EQ(1, failed.count("pa"));
      RPC_RETURN_CODE(0);
    });
    auto result = test.wait(task, std::chrono::seconds{12});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    // 4 次 prepare，且对已响应的 failed_participator 不补发 reject/undo
    CASE_EXPECT_TRUE(dt_test::expect_event_list(
        recorder.events, {"prepare:pa", "prepare:pa", "prepare:pa", "prepare:pa"}));
    // 协调者持久化了 REJECTED 决策（1 次 reject RPC），且本阶段从未走到 commit
    CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()) - reject_calls_before);
    CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit()) - commit_calls_before);
  }

  CASE_EXPECT_EQ(0, test.stop());
}
