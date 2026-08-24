// Copyright 2026 atframework
//
// transaction_participator_handle unit tests: prepare
// storage preservation (DT-001), allow_retry without running state (DT-011), the terminal state
// machine with in-flight direction serialization, Wound-Wait locks (DT-008/DT-022), load/dump
// round trips and the timer-driven resolve flow with independent per-phase budgets
// (DT-005/DT-015/DT-016/DT-020/DT-021).

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

#include <google/protobuf/arena.h>

// windows.h defines GetMessage as a macro, colliding with protobuf Reflection::GetMessage.
#ifdef GetMessage
#  undef GetMessage
#endif

#include <rpc/rpc_context.h>

#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "dispatcher/task_manager.h"
#include "dt_test_common.h"  // NOLINT(build/include_subdir)
#include "rpc/rpc_utils.h"
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "rpc/transaction/transaction_api.h"
#include "transaction_participator_handle.h"  // NOLINT(build/include_subdir)

namespace {
using atframework::distributed_system::EnDistibutedTransactionStatus;
using atframework::distributed_system::SSParticipatorTransactionCommitReq;
using atframework::distributed_system::SSParticipatorTransactionCommitRsp;
using atframework::distributed_system::SSParticipatorTransactionPrepareReq;
using atframework::distributed_system::SSParticipatorTransactionPrepareRsp;
using atframework::distributed_system::SSParticipatorTransactionRejectReq;
using atframework::distributed_system::SSParticipatorTransactionRejectRsp;
using atframework::distributed_system::transaction_metadata;
using atframework::distributed_system::transaction_participator_handle;
using handle_type = transaction_participator_handle;

// Records every vtable callback as an ordered event list and provides per-event failure scripts.
struct participator_event_recorder {
  std::vector<std::string> events;
  std::vector<int32_t> do_event_script;  // consumed FIFO; empty = always success
  std::vector<int32_t> undo_event_script;
  std::vector<int32_t> check_prepare_script;
  std::vector<int32_t> check_writable_script;
  // FIFO of writable out-values; consumed before falling back to check_writable_result.
  // Used to script per-call writability (e.g. losing writability mid-batch).
  std::vector<int32_t> check_writable_values;
  bool check_prepare_allow_retry = false;
  bool check_writable_result = true;
  int check_writable_calls = 0;
  int undo_calls = 0;

  atfw::util::memory::strong_rc_ptr<handle_type::vtable_type> make_vtable() {
    auto vtable = atfw::component::memory::stl::make_strong_rc<handle_type::vtable_type>();
    vtable->do_event = [this](rpc::context&, handle_type&, const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("do_event");
      int32_t res = pop(do_event_script);
      RPC_RETURN_CODE(res);
    };
    vtable->undo_event = [this](rpc::context&, handle_type&,
                                const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("undo_event");
      ++undo_calls;
      int32_t res = pop(undo_event_script);
      RPC_RETURN_CODE(res);
    };
    vtable->check_prepare =
        [this](
            rpc::context&, handle_type&, handle_type::storage_type&,
            atframework::distributed_system::transaction_participator_failure_reason& reason) -> rpc::result_code_type {
      events.emplace_back("check_prepare");
      if (check_prepare_allow_retry) {
        reason.set_allow_retry(true);
      }
      RPC_RETURN_CODE(pop(check_prepare_script));
    };
    vtable->check_writable = [this](rpc::context&, handle_type&, bool& writable) -> rpc::result_code_type {
      ++check_writable_calls;
      if (!check_writable_values.empty()) {
        writable = pop(check_writable_values) != 0;
      } else {
        writable = check_writable_result;
      }
      RPC_RETURN_CODE(pop(check_writable_script));
    };
    vtable->on_start_running = [this](rpc::context&, handle_type&,
                                      const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("on_start_running");
      RPC_RETURN_CODE(0);
    };
    vtable->on_finish_running = [this](rpc::context&, handle_type&,
                                       const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("on_finish_running");
      RPC_RETURN_CODE(0);
    };
    vtable->on_commited = [this](rpc::context&, handle_type&,
                                 const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("on_commited");
      RPC_RETURN_CODE(0);
    };
    vtable->on_rejected = [this](rpc::context&, handle_type&,
                                 const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("on_rejected");
      RPC_RETURN_CODE(0);
    };
    vtable->on_finished = [this](rpc::context&, handle_type&,
                                 const handle_type::storage_type&) -> rpc::result_code_type {
      events.emplace_back("on_finished");
      RPC_RETURN_CODE(0);
    };
    vtable->on_resolve_task_finished = [this](rpc::context&, handle_type&) -> rpc::result_code_type {
      events.emplace_back("on_resolve_task_finished");
      RPC_RETURN_CODE(0);
    };
    return vtable;
  }

  static int32_t pop(std::vector<int32_t>& script) {
    if (script.empty()) {
      return 0;
    }
    int32_t res = script.front();
    script.erase(script.begin());
    return res;
  }

  bool contains(const std::string& needle) const {
    for (const auto& event : events) {
      if (event == needle) {
        return true;
      }
    }
    return false;
  }

  size_t count(const std::string& needle) const {
    size_t res = 0;
    for (const auto& event : events) {
      if (event == needle) {
        ++res;
      }
    }
    return res;
  }
};

using storage_ptr_type = handle_type::storage_ptr_type;

// Builds a prepare request storage with the given parameters. expire_in controls the initial
// resolve deadline (the first query timer fires at expire_timepoint).
SSParticipatorTransactionPrepareReq make_prepare_request(
    gsl::string_view uuid, uint32_t resolve_max_times = 3,
    std::chrono::milliseconds expire_in = std::chrono::milliseconds{20},
    const std::vector<std::string>& lock_resources = {}, uint32_t resolve_times = 0) {
  SSParticipatorTransactionPrepareReq request;
  auto* storage = request.mutable_storage();
  storage->mutable_metadata()->set_transaction_uuid(uuid.data(), uuid.size());
  storage->mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  auto now = std::chrono::system_clock::now();
  auto* prepare_timepoint = storage->mutable_metadata()->mutable_prepare_timepoint();
  prepare_timepoint->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
  prepare_timepoint->set_nanos(static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() % 1000000000));
  auto expire = now + expire_in;
  auto* expire_timepoint = storage->mutable_metadata()->mutable_expire_timepoint();
  expire_timepoint->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(expire.time_since_epoch()).count());
  expire_timepoint->set_nanos(static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(expire.time_since_epoch()).count() % 1000000000));

  storage->mutable_configure()->set_resolve_max_times(resolve_max_times);
  storage->mutable_configure()->mutable_resolve_retry_interval()->set_nanos(10000000);  // 10ms
  storage->set_resolve_times(resolve_times);
  for (const auto& resource : lock_resources) {
    storage->add_lock_resource(resource);
  }
  atframework::distributed_system::transaction_participator_failure_reason sample_data;
  sample_data.set_allow_retry(true);
  ATFW_EXPLICIT_UNUSED_ATTR bool packed = storage->mutable_participator_data()->PackFrom(sample_data);
  return request;
}

// The participator-side merge only reads the coordinator response when the handle's participator
// key exists in the storage participator map, so the mock response must contain it.
atfw::testing::ss_rule_handle register_query_mock(atfw::testing::runtime& test, EnDistibutedTransactionStatus terminal,
                                                  int* call_count = nullptr, gsl::string_view participator_key = "p") {
  return test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      atfw::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [terminal, call_count, participator_key](const atfw::testing::ss_request_view& request,
                                               google::protobuf::Message& response) -> rpc::result_code_type {
        if (nullptr != call_count) {
          ++*call_count;
        }
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<atfw::distributed_system::SSDistributeTransactionQueryRsp&>(response);
        auto* query_storage = typed_response.mutable_storage();
        protobuf_copy_message(*query_storage->mutable_metadata(), typed_request.metadata());
        query_storage->mutable_metadata()->set_status(terminal);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        auto& mock_participator = (*query_storage->mutable_participators())[std::string{participator_key}];
        mock_participator.set_participator_key(std::string{participator_key});
        mock_participator.set_participator_status(terminal);
        RPC_RETURN_CODE(0);
      });
}

atfw::testing::ss_rule_handle register_participator_ack_mock(atfw::testing::runtime& test,
                                                             gsl::string_view commit_or_reject, int* call_count,
                                                             std::vector<int32_t>* fail_script = nullptr) {
  bool is_commit = commit_or_reject == "commit";
  gsl::string_view rpc_name = is_commit ? rpc::transaction::packer::get_full_name_of_commit_participator()
                                        : rpc::transaction::packer::get_full_name_of_reject_participator();
  const auto* request_descriptor =
      is_commit ? static_cast<const google::protobuf::Descriptor*>(
                      atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor())
                : static_cast<const google::protobuf::Descriptor*>(
                      atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor());
  const auto* response_descriptor =
      is_commit ? static_cast<const google::protobuf::Descriptor*>(
                      atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor())
                : static_cast<const google::protobuf::Descriptor*>(
                      atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp::descriptor());
  return test.ss().mock(
      rpc_name, request_descriptor->full_name(), response_descriptor->full_name(),
      [call_count, fail_script](const atfw::testing::ss_request_view& request,
                                google::protobuf::Message& response) -> rpc::result_code_type {
        if (nullptr != call_count) {
          ++*call_count;
        }
        if (nullptr != fail_script && !fail_script->empty()) {
          int32_t res = fail_script->front();
          fail_script->erase(fail_script->begin());
          RPC_RETURN_CODE(res);
        }
        const auto& typed_request = static_cast<const google::protobuf::Message&>(request.body);
        auto& typed_response = response;
        // Fill the metadata field of the typed response via reflection (field name "metadata").
        const auto* descriptor = typed_request.GetDescriptor();
        const auto* request_metadata_field = descriptor->FindFieldByName("metadata");
        const auto* response_metadata_field = typed_response.GetDescriptor()->FindFieldByName("metadata");
        if (nullptr != request_metadata_field && nullptr != response_metadata_field) {
          const auto& metadata = typed_request.GetReflection()->GetMessage(typed_request, request_metadata_field);
          auto* response_metadata =
              typed_response.GetReflection()->MutableMessage(&typed_response, response_metadata_field);
          response_metadata->CopyFrom(metadata);
        }
        RPC_RETURN_CODE(0);
      });
}

// Drives the registered resolve timer at its exact business deadline until pred() is true. The
// steady-clock timeout inside wait_for only guards a hung test; it is not a business-time oracle.
bool drive_handle(atfw::testing::runtime& test, const atfw::util::memory::strong_rc_ptr<handle_type>& handle,
                  const std::function<bool()>& pred,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds{4000}) {
  return dt_test::wait_for(
      test,
      [&handle, &pred]() {
        if (!pred() && handle->has_resolve_custom_timer_for_unit_test()) {
          handle->fire_resolve_custom_timer_for_unit_test(handle->get_resolve_custom_timer_timepoint_for_unit_test());
        }
        return pred();
      },
      timeout);
}
}  // namespace

// ============ construction, key fidelity and destroy callback ============

CASE_TEST(component_distributed_transaction_participator, ctor_key_and_destroy) {
  static int destroy_count = 0;
  destroy_count = 0;
  {
    auto vtable = atfw::component::memory::stl::make_strong_rc<handle_type::vtable_type>();
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "participator-key-1");
    CASE_EXPECT_EQ(std::string("participator-key-1"), handle->get_participator_key());
    CASE_EXPECT_EQ(nullptr, handle->get_private_data());
    handle->set_private_data(handle.get());
    CASE_EXPECT_EQ(handle.get(), handle->get_private_data());

    auto callback = [](handle_type*) { ++destroy_count; };
    handle->set_on_destroy_callback(callback);
    CASE_EXPECT_TRUE(nullptr != handle->get_on_destroy_callback());
    handle.reset();
  }
  CASE_EXPECT_EQ(1, destroy_count);

  // A handle without an active task releases its strong reference cleanly.
  {
    auto vtable = atfw::component::memory::stl::make_strong_rc<handle_type::vtable_type>();
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "k");
    CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
    CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
    atframework::distributed_system::transaction_participator_snapshot snapshot;
    handle->dump(snapshot);
    CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());
  }
}

// ============ DT-001: prepare preserves the request storage (non-Arena and Arena) ============

CASE_TEST(component_distributed_transaction_participator, first_prepare_non_arena_preserves_storage_dt001) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "prepare_non_arena", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-1", 3, std::chrono::milliseconds{60000}, {"lock-a", "lock-b"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        int32_t res = RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!output);
        CASE_EXPECT_EQ("part-uuid-1", output->metadata().transaction_uuid());
        CASE_EXPECT_EQ(3, output->configure().resolve_max_times());
        CASE_EXPECT_TRUE(output->has_participator_data());
        CASE_EXPECT_EQ(2, output->lock_resource_size());
        // Auto-lock: the lock resources of the prepare request are locked into the lock map.
        CASE_EXPECT_TRUE(!!handle->get_locker("lock-a"));
        CASE_EXPECT_TRUE(!!handle->get_locker("lock-b"));
        CASE_EXPECT_EQ(output.get(), handle->get_locker("lock-a").get());

        // DT-001 cross-Arena variant: a context-arena-allocated request keeps its storage too.
        rpc::context::message_holder<SSParticipatorTransactionPrepareReq> arena_request(ctx);
        auto* arena_storage = arena_request->mutable_storage();
        arena_storage->mutable_metadata()->set_transaction_uuid("part-uuid-arena");
        arena_storage->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        arena_storage->mutable_configure()->set_resolve_max_times(2);
        arena_storage->mutable_configure()->mutable_resolve_retry_interval()->set_nanos(10000000);
        SSParticipatorTransactionPrepareRsp arena_response;
        storage_ptr_type arena_output;
        // The request is moved out of the holder: when the task owns an arena the storage is
        // arena-allocated and the running entry is built through the cross-Arena move path.
        res = RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(*arena_request), arena_response, arena_output));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(!!arena_output);
        CASE_EXPECT_EQ("part-uuid-arena", arena_output->metadata().transaction_uuid());
        CASE_EXPECT_EQ(2, arena_output->configure().resolve_max_times());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Empty UUID is rejected.
  auto guard_task = test.run_task(
      "prepare_empty_uuid", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        SSParticipatorTransactionPrepareReq empty_request;
        SSParticipatorTransactionPrepareRsp empty_response;
        storage_ptr_type empty_output;
        CASE_EXPECT_EQ(
            PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
            RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(empty_request), empty_response, empty_output)));
        RPC_RETURN_CODE(0);
      });
  auto guard_result = test.wait(guard_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(guard_result.task_exited);
  CASE_EXPECT_EQ(0, guard_result.result_code);

  CASE_EXPECT_EQ(2, handle->get_running_transactions().size());
  // Lifecycle: each first prepare fires check_prepare + on_start_running exactly once.
  CASE_EXPECT_EQ(2, recorder.count("check_prepare"));
  CASE_EXPECT_EQ(2, recorder.count("on_start_running"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-011: check_prepare 0 + allow_retry does not enter running ============

CASE_TEST(component_distributed_transaction_participator, allow_retry_does_not_enter_running_dt011) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  recorder.check_prepare_allow_retry = true;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "prepare_allow_retry", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-retry");
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        int32_t res = RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output));
        // 0 + allow_retry is returned as-is: the initiator keeps the reason and retries later.
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_TRUE(response.reason().allow_retry());
        CASE_EXPECT_FALSE(!!output);
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // No running/finished/lock state and no lifecycle callbacks fired.
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  CASE_EXPECT_FALSE(!!handle->get_locker("lock-a"));
  CASE_EXPECT_EQ(1, recorder.count("check_prepare"));
  CASE_EXPECT_EQ(0, recorder.count("on_start_running"));
  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  // Nothing is scheduled: ticking far into the future does nothing.
  auto tick_context = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(tick_context, std::chrono::system_clock::now() + std::chrono::hours{1}));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ terminal state machine: commit / reject / idempotency ============

CASE_TEST(component_distributed_transaction_participator, commit_reject_lifecycle) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("commit_lifecycle", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-commit", 3, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));

        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("part-uuid-commit");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));

        // The transaction moved from running to finished (COMMITING = commit-direction terminal that
        // still owes the coordinator an ack).
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
        auto finished_iter = handle->get_finished_transactions().find("part-uuid-commit");
        CASE_EXPECT_TRUE(finished_iter != handle->get_finished_transactions().end());
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                       finished_iter->second->metadata().status());

        // Duplicate commit is idempotent: no second do_event, no state change.
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));

        // Reject after commit: the transaction is no longer running, so it is idempotent success
        // without firing the reject lifecycle.
        SSParticipatorTransactionRejectReq reject_request;
        reject_request.set_transaction_uuid("part-uuid-commit");
        SSParticipatorTransactionRejectRsp reject_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                       finished_iter->second->metadata().status());

        // UUID not found is idempotent success.
        SSParticipatorTransactionCommitReq absent_request;
        absent_request.set_transaction_uuid("part-uuid-absent");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, absent_request, commit_response)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Lifecycle order for commit: do_event -> on_finish_running -> on_finished -> on_commited.
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"check_prepare", "on_start_running", "do_event",
                                                                "on_finish_running", "on_finished", "on_commited"}));

  // force_commit prepare path: completes inside prepare with no running/finished state.
  recorder.events.clear();
  auto force_task = test.run_task(
      "force_commit_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-force", 3, std::chrono::milliseconds{60000});
        request.mutable_storage()->mutable_configure()->set_force_commit(true);
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        CASE_EXPECT_FALSE(!!output);

        // force_commit undo with a mismatched inner UUID is rejected before undo_event runs.
        SSParticipatorTransactionRejectReq undo_request;
        undo_request.set_transaction_uuid("part-uuid-force");
        auto* undo_storage = undo_request.mutable_storage();
        undo_storage->mutable_configure()->set_force_commit(true);
        undo_storage->mutable_metadata()->set_transaction_uuid("part-uuid-mismatch");
        SSParticipatorTransactionRejectRsp undo_response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                       RPC_AWAIT_CODE_RESULT(handle->reject(ctx, undo_request, undo_response)));
        RPC_RETURN_CODE(0);
      });
  auto force_result = test.wait(force_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(force_result.task_exited);
  CASE_EXPECT_EQ(0, force_result.result_code);
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  // force_commit never enters running/finished; its lifecycle is
  // on_start_running -> do_event -> on_finish_running -> on_finished -> on_commited.
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"check_prepare", "on_start_running", "do_event",
                                                                "on_finish_running", "on_finished", "on_commited"}));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ force_commit failure keeps undo compensation available ============

CASE_TEST(component_distributed_transaction_participator, force_commit_failure_runs_undo) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  recorder.do_event_script = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  recorder.undo_event_script = {0};
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "force_commit_failure", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-force-fail", 3, std::chrono::milliseconds{60000});
        request.mutable_storage()->mutable_configure()->set_force_commit(true);
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                       RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        CASE_EXPECT_FALSE(!!output);

        // undo with a matching UUID runs undo_event exactly once.
        SSParticipatorTransactionRejectReq undo_request;
        undo_request.set_transaction_uuid("part-uuid-force-fail");
        auto* undo_storage = undo_request.mutable_storage();
        undo_storage->mutable_configure()->set_force_commit(true);
        undo_storage->mutable_metadata()->set_transaction_uuid("part-uuid-force-fail");
        SSParticipatorTransactionRejectRsp undo_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, undo_request, undo_response)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // do_event failed: on_finished/on_commited never fire (header contract), undo runs once.
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_EQ(0, recorder.count("on_finished"));
  CASE_EXPECT_EQ(0, recorder.count("on_commited"));
  CASE_EXPECT_EQ(1, recorder.undo_calls);
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ Wound-Wait locks: check_lock tie-break + preemption (DT-022) ============

CASE_TEST(component_distributed_transaction_participator, lock_wound_and_preemption_dt022) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // The coordinator answers the reject_participator ack of the wounded transaction.
  int reject_ack_calls = 0;
  auto reject_ack_rule = register_participator_ack_mock(test, "reject", &reject_ack_calls);
  CASE_EXPECT_TRUE(!!reject_ack_rule);
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  // vtable 的 lambda 按 this 捕获 recorder；自定义定时器自驱动后，reloaded handle 的 resolve 任务
  // 可能持 strong_rc 越过协程帧结束仍在运行，reload_recorder 必须比在途任务活得更久（测试作用域）
  participator_event_recorder reload_recorder;

  auto task =
      test.run_task("lock_wound", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto older_request = make_prepare_request("part-uuid-older", 2, std::chrono::milliseconds{60000});
        auto younger_request = make_prepare_request("part-uuid-younger", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type older_output;
        storage_ptr_type younger_output;
        CASE_EXPECT_EQ(0,
                       RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(older_request), response, older_output)));
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(younger_request), response, younger_output)));

        // Explicit timestamps on the storage returned by the real prepare flow: older transaction
        // was prepared earlier.
        older_output->mutable_metadata()->mutable_prepare_timepoint()->set_seconds(1000);
        older_output->mutable_metadata()->mutable_prepare_timepoint()->set_nanos(0);
        younger_output->mutable_metadata()->mutable_prepare_timepoint()->set_seconds(2000);
        younger_output->mutable_metadata()->mutable_prepare_timepoint()->set_nanos(0);

        // The older transaction holds res-0 first (lock is only called after check_lock per the
        // contract; lock itself does not re-check ages and always wounds the previous holder).
        google::protobuf::RepeatedPtrField<std::string> older_resources;
        older_resources.Add()->assign("res-0");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(older_output, older_resources)));

        // The younger transaction cannot preempt: check_lock reports the older holder of res-0.
        transaction_metadata younger_metadata;
        younger_metadata.set_transaction_uuid("part-uuid-younger");
        younger_metadata.mutable_prepare_timepoint()->set_seconds(2000);
        std::list<handle_type::storage_const_ptr_type> preemption;
        std::vector<std::string> check_resources = {"res-0"};
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       handle->check_lock(younger_metadata, check_resources, preemption));
        CASE_EXPECT_EQ(1, preemption.size());
        CASE_EXPECT_EQ(std::string("part-uuid-older"), preemption.front()->metadata().transaction_uuid());

        // Wound-Wait: the younger transaction takes the free res-1 first, then the older transaction
        // (earlier prepare timepoint) preempts and wounds the younger holder.
        google::protobuf::RepeatedPtrField<std::string> younger_resources;
        younger_resources.Add()->assign("res-1");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(younger_output, younger_resources)));

        transaction_metadata older_metadata;
        older_metadata.set_transaction_uuid("part-uuid-older");
        older_metadata.mutable_prepare_timepoint()->set_seconds(1000);
        std::vector<std::string> older_check_resources = {"res-1"};
        std::list<handle_type::storage_const_ptr_type> older_preemption;
        CASE_EXPECT_EQ(0, handle->check_lock(older_metadata, older_check_resources, older_preemption));
        google::protobuf::RepeatedPtrField<std::string> wound_resources;
        wound_resources.Add()->assign("res-1");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(older_output, wound_resources)));

        // The wounded younger transaction is rejected from commit without running do_event.
        SSParticipatorTransactionCommitReq wounded_commit;
        wounded_commit.set_transaction_uuid("part-uuid-younger");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       RPC_AWAIT_CODE_RESULT(handle->commit(ctx, wounded_commit, commit_response)));

        // The wound is durable: dump keeps the local rejection intent for the younger transaction.
        atframework::distributed_system::transaction_participator_snapshot snapshot;
        handle->dump(snapshot);
        bool younger_rejecting = false;
        for (const auto& running : snapshot.running_transaction()) {
          if (running.metadata().transaction_uuid() == "part-uuid-younger") {
            younger_rejecting = running.metadata().status() ==
                                EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING;
          }
        }
        CASE_EXPECT_TRUE(younger_rejecting);
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));

  // The wound survives the reload: a fresh handle still refuses commit for the younger transaction.
  auto reload_task = test.run_task(
      "wound_reload", std::chrono::seconds{4}, [&handle, &reload_recorder](rpc::context& ctx) -> rpc::result_code_type {
        atframework::distributed_system::transaction_participator_snapshot snapshot;
        handle->dump(snapshot);
        // reload_recorder 已提升到测试作用域（在途 resolve 任务可能越过本协程帧仍在运行）
        auto reloaded = atfw::component::memory::stl::make_strong_rc<handle_type>(reload_recorder.make_vtable(), "p2");
        reloaded->load(snapshot);
        CASE_EXPECT_EQ(2, reloaded->get_running_transactions().size());

        SSParticipatorTransactionCommitReq wounded_commit;
        wounded_commit.set_transaction_uuid("part-uuid-younger");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       RPC_AWAIT_CODE_RESULT(reloaded->commit(ctx, wounded_commit, commit_response)));
        RPC_RETURN_CODE(0);
      });
  auto reload_result = test.wait(reload_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(reload_result.task_exited);
  CASE_EXPECT_EQ(0, reload_result.result_code);

  // Due timer on the original handle prefers the local reject: the wounded transaction is moved to
  // finished (REJECTING) and the reject ack is delivered; it never stays in running forever. The
  // predicate includes the ack counter because between remove_running and add_finished the entry
  // is momentarily in neither container.
  recorder.events.clear();
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &reject_ack_calls]() {
    return reject_ack_calls >= 1 &&
           handle->get_running_transactions().find("part-uuid-younger") == handle->get_running_transactions().end() &&
           handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_GE(reject_ack_calls, 1);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));  // the wounded path never runs do_event
  CASE_EXPECT_GE(recorder.count("on_rejected"), 1);
  // The older transaction still owns the resource.
  CASE_EXPECT_TRUE(!!handle->get_locker("res-1"));
  CASE_EXPECT_EQ(std::string("part-uuid-older"), handle->get_locker("res-1")->metadata().transaction_uuid());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-008: appended locks are dumped and released ============

CASE_TEST(component_distributed_transaction_participator, appended_lock_is_dumped_and_released_dt008) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("appended_locks", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-locks");
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));

        google::protobuf::RepeatedPtrField<std::string> resource_a;
        resource_a.Add()->assign("res-a");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(output, resource_a)));

        google::protobuf::RepeatedPtrField<std::string> resource_b;
        resource_b.Add()->assign("res-b");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(output, resource_b)));

        // Appended locks accumulate in the storage: dump contains both.
        atframework::distributed_system::transaction_participator_snapshot snapshot;
        handle->dump(snapshot);
        CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
        CASE_EXPECT_EQ(2, snapshot.running_transaction(0).lock_resource_size());

        // Partial unlock frees exactly one resource and its storage entry.
        CASE_EXPECT_TRUE(handle->unlock(output, std::string{"res-a"}));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-a"));
        CASE_EXPECT_TRUE(!!handle->get_locker("res-b"));

        // unlock by UUID frees the rest.
        CASE_EXPECT_TRUE(handle->unlock(std::string{"part-uuid-locks"}));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-b"));
        // storage.lock_resource and lock map are cleared together. output is the same storage
        // returned by the real prepare flow, so no internal container mutation/access is needed.
        CASE_EXPECT_EQ(0, output->lock_resource_size());

        // Unlock mismatches: another holder/no lock/not found.
        CASE_EXPECT_FALSE(handle->unlock(output, std::string{"res-a"}));
        CASE_EXPECT_FALSE(handle->unlock(std::string{"not-running"}));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ check_lock tie-break and guards ============

CASE_TEST(component_distributed_transaction_participator, check_lock_tiebreak_and_guards) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("check_lock", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("part-uuid-holder", 2, std::chrono::milliseconds{60000}, {"res-1", "res-2"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        output->mutable_metadata()->mutable_prepare_timepoint()->set_seconds(1000);
        output->mutable_metadata()->mutable_prepare_timepoint()->set_nanos(100);

        transaction_metadata metadata;
        std::list<handle_type::storage_const_ptr_type> preemption;

        // empty UUID
        std::vector<std::string> resources = {"res-1"};
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, handle->check_lock(metadata, resources, preemption));

        // terminal status
        metadata.set_transaction_uuid("part-uuid-x");
        metadata.mutable_prepare_timepoint()->set_seconds(1000);
        metadata.mutable_prepare_timepoint()->set_nanos(100);
        metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED,
                       handle->check_lock(metadata, resources, preemption));

        // same-timestamp UUID tie-break: the smaller UUID wins (holder "part-uuid-holder" vs the
        // younger "part-uuid-zzz" and the older "part-uuid-aaa").
        metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        metadata.mutable_prepare_timepoint()->set_seconds(1000);
        metadata.mutable_prepare_timepoint()->set_nanos(100);
        metadata.set_transaction_uuid("part-uuid-zzz");  // younger by UUID: cannot preempt
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       handle->check_lock(metadata, resources, preemption));

        metadata.set_transaction_uuid("part-uuid-aaa");  // older by UUID: preempts the holder
        preemption.clear();
        CASE_EXPECT_EQ(0, handle->check_lock(metadata, resources, preemption));

        // nanosecond comparison
        metadata.set_transaction_uuid("part-uuid-zzz");
        metadata.mutable_prepare_timepoint()->set_nanos(50);  // older than the holder's 100ns
        preemption.clear();
        CASE_EXPECT_EQ(0, handle->check_lock(metadata, resources, preemption));

        // free resource: success with an empty preemption list
        metadata.mutable_prepare_timepoint()->set_nanos(100);
        std::vector<std::string> free_resources = {"res-free"};
        preemption.clear();
        CASE_EXPECT_EQ(0, handle->check_lock(metadata, free_resources, preemption));
        CASE_EXPECT_TRUE(preemption.empty());

        // same transaction reentry: a holder checking its own lock succeeds
        metadata.set_transaction_uuid("part-uuid-holder");
        preemption.clear();
        CASE_EXPECT_EQ(0, handle->check_lock(metadata, resources, preemption));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ load/dump round trip ============

CASE_TEST(component_distributed_transaction_participator, load_dump_round_trip) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  // reload_recorder 提升到测试作用域：自定义定时器自驱动后，reloaded handle 的 resolve 任务
  // 可能持 strong_rc 越过协程帧结束仍在运行，recorder 必须比在途任务活得更久
  participator_event_recorder reload_recorder;

  auto task = test.run_task(
      "load_dump", std::chrono::seconds{4}, [&handle, &reload_recorder](rpc::context& ctx) -> rpc::result_code_type {
        // One running (with locks) and one finished transaction.
        auto running_request =
            make_prepare_request("part-uuid-running", 4, std::chrono::milliseconds{60000}, {"res-1"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type running_output;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(running_request), response, running_output)));

        auto finished_request = make_prepare_request("part-uuid-finished", 4, std::chrono::milliseconds{60000});
        storage_ptr_type finished_output;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(finished_request), response, finished_output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("part-uuid-finished");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));

        atframework::distributed_system::transaction_participator_snapshot snapshot;
        handle->dump(snapshot);
        CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
        CASE_EXPECT_EQ(1, snapshot.finished_transaction_size());

        // load replaces the state instead of appending.
        // reload_recorder 在测试作用域声明，与在途 resolve 任务同寿
        auto reloaded = atfw::component::memory::stl::make_strong_rc<handle_type>(reload_recorder.make_vtable(), "p2");
        auto stale_request = make_prepare_request("part-uuid-stale");
        SSParticipatorTransactionPrepareRsp stale_response;
        storage_ptr_type stale_output;
        CASE_EXPECT_EQ(
            0, RPC_AWAIT_CODE_RESULT(reloaded->prepare(ctx, std::move(stale_request), stale_response, stale_output)));
        CASE_EXPECT_EQ(1, reloaded->get_running_transactions().size());

        reloaded->load(snapshot);
        CASE_EXPECT_EQ(1, reloaded->get_running_transactions().size());
        CASE_EXPECT_EQ(1, reloaded->get_finished_transactions().size());
        CASE_EXPECT_EQ(0, reloaded->get_running_transactions().count("part-uuid-stale"));
        CASE_EXPECT_TRUE(!!reloaded->get_locker("res-1"));
        CASE_EXPECT_EQ(std::string("part-uuid-running"), reloaded->get_locker("res-1")->metadata().transaction_uuid());

        // Duplicate running entries in one snapshot keep the first occurrence.
        auto* duplicate = snapshot.add_running_transaction();
        duplicate->mutable_metadata()->set_transaction_uuid("part-uuid-running");
        duplicate->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        reloaded->load(snapshot);
        CASE_EXPECT_EQ(1, reloaded->get_running_transactions().size());

        // Empty-UUID entries are skipped.
        snapshot.add_finished_transaction();
        reloaded->load(snapshot);
        CASE_EXPECT_EQ(1, reloaded->get_finished_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ resolve flow: coordinator COMMITED -> do_event + ack (DT-005/DT-021) ============

CASE_TEST(component_distributed_transaction_participator, resolve_query_committed_dt005_dt021) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int commit_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("resolve_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-resolve-commit", 2, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // The first query timer fires at expire_timepoint; the coordinator reports COMMITED; do_event
  // runs once; the finished ack delivers commit_participator (DT-021: the ack direction follows
  // the resolved terminal, not the storage status field name).
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &query_calls]() {
    return query_calls >= 1 && handle->get_running_transactions().empty() &&
           handle->get_finished_transactions().empty();
  }));

  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_GE(commit_ack_calls, 1);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_GE(recorder.count("on_resolve_task_finished"), 1);

  // Full local consumption: running/finished/dump/locks are all empty.
  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  // The empty queue disarmed the custom timer (no self-driving, no tight loop). A tick with a
  // far-future timepoint still collects nothing, so do_event never runs again.
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  auto tick_context = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(tick_context, std::chrono::system_clock::now() + std::chrono::hours{1}));
  CASE_EXPECT_EQ(1, recorder.count("do_event"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-005/DT-020: local action failures are bounded and consumed ============

CASE_TEST(component_distributed_transaction_participator, terminal_action_failure_is_bounded_dt005_dt020) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int commit_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  recorder.do_event_script = {-1, -1, -1, -1, -1, -1, -1, -1};  // always fails
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "action_failure_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        // resolve_max_times = 2: exactly 2 real do_event attempts before the consumption.
        auto request = make_prepare_request("part-uuid-action-fail", 2, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // The do_event budget is not cleared by the query phase re-entry (DT-020): exactly 2 attempts,
  // then the transaction is consumed by the coordinator decision and the ack still goes out.
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &recorder]() {
    return recorder.count("do_event") >= 2 && handle->get_running_transactions().empty() &&
           handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_EQ(2, recorder.count("do_event"));  // bounded: never a third attempt
  CASE_EXPECT_GE(commit_ack_calls, 1);

  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-015: resolve budget exhaustion rejects without querying ============

CASE_TEST(component_distributed_transaction_participator, resolve_budget_exhaustion_rejects_dt015) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int reject_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                                        &query_calls);
  auto reject_ack_rule = register_participator_ack_mock(test, "reject", &reject_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!reject_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "budget_exhaustion", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        // resolve_times == resolve_max_times already: the next resolve entry goes straight to the
        // local reject, with zero coordinator queries.
        auto request = make_prepare_request("part-uuid-budget", 2, std::chrono::milliseconds{20}, {}, 2);
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle]() {
    return handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_EQ(0, query_calls);  // D6: exhausted budget never queries again
  CASE_EXPECT_GE(reject_ack_calls, 1);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_GE(recorder.count("on_rejected"), 1);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ resolve: notfound releases everything without an ack ============

CASE_TEST(component_distributed_transaction_participator, resolve_notfound_cleans_up) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  auto query_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_query(),
                                         PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("notfound_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-notfound", 2, std::chrono::milliseconds{20}, {"res-nf"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle]() {
    return handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
  }));

  // NOTFOUND cleanup: running/lock/timer released; only on_finish_running fires (the transaction
  // has no global terminal, so on_finished/on_rejected must not) and no ack is sent.
  CASE_EXPECT_FALSE(!!handle->get_locker("res-nf"));
  CASE_EXPECT_EQ(1, recorder.count("on_finish_running"));
  CASE_EXPECT_EQ(0, recorder.count("on_finished"));
  CASE_EXPECT_EQ(0, recorder.count("on_rejected"));
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ DT-016: finished ack failures are bounded and finally consumed ============

CASE_TEST(component_distributed_transaction_participator, finished_ack_failure_has_budget_dt016) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int commit_ack_calls = 0;
  std::vector<int32_t> ack_failures = {-1, -1, -1, -1, -1, -1, -1, -1};  // always fail
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls, &ack_failures);
  CASE_EXPECT_TRUE(!!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "ack_budget_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-ack-fail", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));

        // Drive straight into finished: do_event succeeds, the ack keeps failing.
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("part-uuid-ack-fail");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // resolve_max_times = 2: exactly 2 ack attempts, then the finished entry is consumed locally.
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &commit_ack_calls]() {
    return commit_ack_calls >= 2 && handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_EQ(2, commit_ack_calls);

  // No further ticks send more acks (no tight loop); the coordinator TTL is the final backstop.
  auto tick_context = atfw::testing::make_context();
  for (int i = 0; i < 8; ++i) {
    handle->tick(tick_context, std::chrono::system_clock::now() + std::chrono::hours{1});
    test.pump_once();
  }
  CASE_EXPECT_EQ(2, commit_ack_calls);
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 5.3.5: check_writable=false defers the whole queue ============

CASE_TEST(component_distributed_transaction_participator, check_writable_defers_queue) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  recorder.check_writable_result = false;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("writable_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        bool writable = true;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->check_writable(ctx, writable)));
        CASE_EXPECT_FALSE(writable);

        auto request = make_prepare_request("part-uuid-readonly", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Trigger the exact stored deadline. The resolve task observes non-writable and defers the
  // transaction without querying or consuming it.
  auto tick_context = atfw::testing::make_context();
  auto running_iter = handle->get_running_transactions().find("part-uuid-readonly");
  CASE_EXPECT_TRUE(running_iter != handle->get_running_transactions().end());
  if (running_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  auto resolve_due = protobuf_to_system_clock(running_iter->second.storage->resolve_timepoint());
  CASE_EXPECT_EQ(0, handle->tick(tick_context, resolve_due));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder, &handle]() {
    return recorder.check_writable_calls >= 2 && handle->has_resolve_custom_timer_for_unit_test();
  }));
  CASE_EXPECT_EQ(0, query_calls);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().size());

  // Recovering writability resumes the flow.
  recorder.check_writable_result = true;
  running_iter = handle->get_running_transactions().find("part-uuid-readonly");
  CASE_EXPECT_TRUE(running_iter != handle->get_running_transactions().end());
  if (running_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  resolve_due = protobuf_to_system_clock(running_iter->second.storage->resolve_timepoint());
  CASE_EXPECT_EQ(0, handle->tick(tick_context, resolve_due));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&handle, &query_calls]() {
    return query_calls >= 1 && handle->get_running_transactions().empty() &&
           handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ check_prepare errors propagate without entering running (4.3 prepare row) ============

CASE_TEST(component_distributed_transaction_participator, check_prepare_error_propagates) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  recorder.check_prepare_script = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "check_prepare_error", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-check-error");
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        int32_t res = RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, res);
        CASE_EXPECT_FALSE(!!output);
        CASE_EXPECT_FALSE(response.reason().allow_retry());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // A definitive check_prepare error leaves no state behind and starts no lifecycle.
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  CASE_EXPECT_EQ(1, recorder.count("check_prepare"));
  CASE_EXPECT_EQ(0, recorder.count("on_start_running"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 5.3.1: coordinator REJECTED drives the local reject direction ============

CASE_TEST(component_distributed_transaction_participator, resolve_query_rejected_runs_reject_direction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int reject_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                                        &query_calls);
  auto reject_ack_rule = register_participator_ack_mock(test, "reject", &reject_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!reject_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "resolve_rejected_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-resolve-reject", 2, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &reject_ack_calls]() {
    return reject_ack_calls >= 1 && handle->get_running_transactions().empty() &&
           handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_GE(reject_ack_calls, 1);
  // The reject direction never runs do_event and fires the reject lifecycle.
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_GE(recorder.count("on_rejected"), 1);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 5.3.2 prelude: PREPARED answers only requeue the timer ============

CASE_TEST(component_distributed_transaction_participator, resolve_query_prepared_only_requeues) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                                        &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "resolve_prepared_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        // resolve_max_times = 2: two PREPARED queries requeue the timer, the third entry exceeds the
        // budget and goes straight to the local reject without another query.
        auto request = make_prepare_request("part-uuid-resolve-prepared", 2, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle]() {
    return handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
  }));
  // Exactly resolve_max_times queries happened; every PREPARED answer only requeued the timer, so
  // no business action ran before the budget-exceeded local reject.
  CASE_EXPECT_EQ(2, query_calls);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_GE(recorder.count("on_rejected"), 1);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ check_lock with multiple and duplicate resources (4.3 row) ============

CASE_TEST(component_distributed_transaction_participator, check_lock_multiple_and_duplicate_resources) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("check_lock_multi", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("part-uuid-holder-multi", 2, std::chrono::milliseconds{60000}, {"res-1", "res-2"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        output->mutable_metadata()->mutable_prepare_timepoint()->set_seconds(1000);
        output->mutable_metadata()->mutable_prepare_timepoint()->set_nanos(0);

        // A younger foreign transaction gets preempted by the older holder of res-1/res-2.
        transaction_metadata metadata;
        metadata.set_transaction_uuid("part-uuid-younger-multi");
        metadata.mutable_prepare_timepoint()->set_seconds(2000);
        metadata.set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        std::list<handle_type::storage_const_ptr_type> preemption;

        // Mixed held/free resources: only the actually held one is reported, once per mention.
        std::vector<std::string> mixed = {"res-1", "res-free"};
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       handle->check_lock(metadata, mixed, preemption));
        CASE_EXPECT_EQ(1, preemption.size());

        // Duplicate held resources are reported once per mention.
        std::vector<std::string> duplicated = {"res-1", "res-1"};
        preemption.clear();
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       handle->check_lock(metadata, duplicated, preemption));
        CASE_EXPECT_EQ(2, preemption.size());

        // Multiple free resources succeed with an empty list.
        std::vector<std::string> free_resources = {"res-free", "res-free-2"};
        preemption.clear();
        CASE_EXPECT_EQ(0, handle->check_lock(metadata, free_resources, preemption));
        CASE_EXPECT_TRUE(preemption.empty());

        // The holder itself reentering across multiple held resources succeeds.
        transaction_metadata own_metadata;
        own_metadata.set_transaction_uuid("part-uuid-holder-multi");
        own_metadata.mutable_prepare_timepoint()->set_seconds(1000);
        std::vector<std::string> own = {"res-1", "res-2"};
        preemption.clear();
        CASE_EXPECT_EQ(0, handle->check_lock(own_metadata, own, preemption));
        CASE_EXPECT_TRUE(preemption.empty());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 4.3/5.3.8: in-flight terminal direction serializes concurrent commit/reject ============

CASE_TEST(component_distributed_transaction_participator, concurrent_terminal_direction_is_serialized) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  // do_event parks behind an explicit gate so the commit transition is observably in flight.
  int do_event_entered = 0;
  bool release_do_event = false;
  auto vtable = recorder.make_vtable();
  auto original_do_event = vtable->do_event;
  vtable->do_event = [&do_event_entered, &release_do_event, &original_do_event](
                         rpc::context& ctx, handle_type& self,
                         const handle_type::storage_type& storage) -> rpc::result_code_type {
    ++do_event_entered;
    for (int i = 0; i < 5000 && !release_do_event; ++i) {
      RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
    }
    if (!release_do_event) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    }
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(original_do_event(ctx, self, storage)));
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "concurrent_terminal_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-concurrent", 3, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Task A starts commit and parks inside do_event.
  SSParticipatorTransactionCommitReq commit_request;
  commit_request.set_transaction_uuid("part-uuid-concurrent");
  auto commit_task =
      test.run_task("concurrent_commit", std::chrono::seconds{4},
                    [&handle, commit_request](rpc::context& ctx) -> rpc::result_code_type {
                      SSParticipatorTransactionCommitRsp commit_response;
                      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
                      RPC_RETURN_CODE(0);
                    });
  // Wait for the business callback itself, not a guessed number of event-loop generations.
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&do_event_entered]() { return do_event_entered == 1; }));
  CASE_EXPECT_EQ(1, do_event_entered);

  // While the commit transition is in flight, the reverse direction is refused and the same
  // direction is idempotent; neither runs a second business action.
  SSParticipatorTransactionRejectReq reject_request;
  reject_request.set_transaction_uuid("part-uuid-concurrent");
  auto reject_task =
      test.run_task("concurrent_reject", std::chrono::seconds{4},
                    [&handle, reject_request](rpc::context& ctx) -> rpc::result_code_type {
                      SSParticipatorTransactionRejectRsp reject_response;
                      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED,
                                     RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
                      RPC_RETURN_CODE(0);
                    });
  auto commit_task2 =
      test.run_task("concurrent_commit_repeat", std::chrono::seconds{4},
                    [&handle, commit_request](rpc::context& ctx) -> rpc::result_code_type {
                      SSParticipatorTransactionCommitRsp commit_response;
                      CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
                      RPC_RETURN_CODE(0);
                    });

  auto reject_result = test.wait(reject_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(reject_result.task_exited);
  CASE_EXPECT_EQ(0, reject_result.result_code);
  auto repeat_result = test.wait(commit_task2, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(repeat_result.task_exited);
  CASE_EXPECT_EQ(0, repeat_result.result_code);
  CASE_EXPECT_EQ(1, do_event_entered);  // the in-flight direction is the only one running

  release_do_event = true;
  auto commit_result = test.wait(commit_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(commit_result.task_exited);
  CASE_EXPECT_EQ(0, commit_result.result_code);
  CASE_EXPECT_EQ(1, do_event_entered);  // the successful business action ran exactly once
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  // The commit direction finished; the reject lifecycle never fired.
  CASE_EXPECT_EQ(0, recorder.count("on_rejected"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 4.3: get_locker after the holder finished (locks released) ============

CASE_TEST(component_distributed_transaction_participator, get_locker_released_after_finish) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task =
      test.run_task("locker_edges", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-locker", 2, std::chrono::milliseconds{60000}, {"res-locker"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        CASE_EXPECT_TRUE(!!handle->get_locker("res-locker"));

        // Committing releases the resource locks together with the running entry.
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("part-uuid-locker");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-locker"));  // no dangling strong reference
        CASE_EXPECT_FALSE(!!handle->get_locker("never-locked"));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ 4.3: tick processes a timer due exactly at the given timepoint ============

CASE_TEST(component_distributed_transaction_participator, tick_equal_timepoint_is_due) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                                        &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "equal_timepoint_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-equal", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // The initial resolve timer equals the transaction expire timepoint exactly; a tick before it
  // collects nothing and a tick exactly at it collects (trigger_due uses a strict > comparison).
  auto tick_context = atfw::testing::make_context();
  auto running_iter = handle->get_running_transactions().find("part-uuid-equal");
  CASE_EXPECT_TRUE(running_iter != handle->get_running_transactions().end());
  if (running_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  auto before_expire = protobuf_to_system_clock(running_iter->second.storage->metadata().expire_timepoint()) -
                       std::chrono::milliseconds{1};
  // Not due yet: trigger_due synchronously keeps the queue entry, so the custom timer still points
  // at the stored resolve timepoint and no resolve task was launched (query_calls stays 0). The
  // queue/timer state is the deterministic oracle; a delivered query would need pump generations.
  CASE_EXPECT_EQ(0, handle->tick(tick_context, before_expire));
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_TRUE(handle->get_resolve_custom_timer_timepoint_for_unit_test() ==
                   protobuf_to_system_clock(running_iter->second.storage->resolve_timepoint()));
  CASE_EXPECT_EQ(0, query_calls);

  auto exactly_expire = protobuf_to_system_clock(running_iter->second.storage->metadata().expire_timepoint());
  handle->tick(tick_context, exactly_expire);
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&query_calls]() { return query_calls >= 1; }));
  CASE_EXPECT_GE(query_calls, 1);

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ section 3.1 seam: tick task-launch failure re-arms with backoff ============

CASE_TEST(component_distributed_transaction_participator, tick_launch_failure_rearms_with_backoff) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  // Two running transactions with due resolve timers (expire after 20ms, retry interval 10ms).
  auto task =
      test.run_task("seam_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request_a = make_prepare_request("part-uuid-seam-a", 3, std::chrono::milliseconds{20});
        auto request_b = make_prepare_request("part-uuid-seam-b", 3, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_a), response, output)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_b), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Install an interception mock hook that fails only the resolve action; both transactions keep
  // running afterwards. The hook is scoped (RAII) and force-cleared at the end of the case.
  task_manager::mock_create_task_clear();  // 防御：确保用例起始无泄漏钩子
  auto failure_hook = task_manager::mock_create_task(
      [](gsl::string_view demangled_task_type_name, std::chrono::system_clock::duration&) -> int {
        if (demangled_task_type_name.find("task_action_participator_resolve_transaction") != gsl::string_view::npos) {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        }
        return 0;
      });
  CASE_EXPECT_TRUE(static_cast<bool>(failure_hook));

  auto tick_context = atfw::testing::make_context();
  auto& running_map = handle->get_running_transactions();
  auto seam_a_iter = running_map.find("part-uuid-seam-a");
  auto seam_b_iter = running_map.find("part-uuid-seam-b");
  CASE_EXPECT_TRUE(seam_a_iter != running_map.end() && seam_b_iter != running_map.end());
  if (seam_a_iter == running_map.end() || seam_b_iter == running_map.end()) {
    task_manager::mock_create_task_clear();
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto first_due = protobuf_to_system_clock(seam_a_iter->second.storage->resolve_timepoint());

  // First tick: the collected timers hand over ownership to the task launch, which fails; tick
  // reports the injected error and everything collected must be re-armed with backoff.
  int tick_result = handle->tick(tick_context, first_due);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC, tick_result);
  CASE_EXPECT_EQ(2, handle->get_running_transactions().size());
  CASE_EXPECT_EQ(0, query_calls);  // infrastructure failure never starts the resolve flow
  CASE_EXPECT_EQ(0, recorder.count("on_resolve_task_finished"));
  CASE_EXPECT_EQ(0, recorder.count("do_event"));

  // Neither transaction consumed any retry budget: infrastructure failures do not count.
  CASE_EXPECT_EQ(0, seam_a_iter->second.storage->resolve_times());
  CASE_EXPECT_EQ(0, seam_b_iter->second.storage->resolve_times());

  // A tick immediately before the explicitly re-armed deadline does not relaunch anything.
  auto retry_due = protobuf_to_system_clock(seam_a_iter->second.storage->resolve_timepoint());
  CASE_EXPECT_EQ(0, handle->tick(tick_context, retry_due - atfw::util::time::time_utility::raw_time_t::duration{1}));
  CASE_EXPECT_EQ(0, query_calls);
  CASE_EXPECT_EQ(0, seam_a_iter->second.storage->resolve_times());

  // At the stored deadline the launch fails again with the same backoff semantics and no budget use.
  tick_result = handle->tick(tick_context, retry_due);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC, tick_result);
  CASE_EXPECT_EQ(0, query_calls);
  CASE_EXPECT_EQ(2, handle->get_running_transactions().size());
  CASE_EXPECT_EQ(0, seam_a_iter->second.storage->resolve_times());

  // Release the hook: the next due tick launches the resolve task and both transactions
  // recover through the normal flow (query -> COMMITED -> do_event -> ack -> consumed).
  failure_hook.reset();
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &query_calls]() {
    return query_calls >= 2 && handle->get_running_transactions().empty() &&
           handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_GE(query_calls, 2);
  CASE_EXPECT_EQ(2, recorder.count("do_event"));
  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  // Hook hygiene: nothing survives the case.
  failure_hook.reset();
  CASE_EXPECT_FALSE(task_manager::mock_create_task_active());
  task_manager::mock_create_task_clear();

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ atapp 自定义定时器自驱动：无需外部周期调用 tick() ============

// 验收：prepare 后不调用任何 handle->tick()，仅由 atapp 自定义定时器到期驱动完整生命周期
// （query -> COMMITED -> do_event -> ack -> 全部消费），且消费完毕后定时器被撤销。
CASE_TEST(component_distributed_transaction_participator, custom_timer_drives_full_lifecycle_without_external_tick) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int commit_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task(
      "custom_timer_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-auto-timer", 2, std::chrono::milliseconds{30});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // 定时器已注册且指向最早事件（初始 resolve 时间点 == expire_timepoint）
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  auto running_iter = handle->get_running_transactions().find("part-uuid-auto-timer");
  CASE_EXPECT_TRUE(running_iter != handle->get_running_transactions().end());
  if (running_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  CASE_EXPECT_TRUE(handle->get_resolve_custom_timer_timepoint_for_unit_test() ==
                   protobuf_to_system_clock(running_iter->second.storage->resolve_timepoint()));

  // 关键：不做任何 handle->tick()，仅 pump 事件循环；自定义定时器必须自行驱动完整生命周期
  CASE_EXPECT_TRUE(dt_test::wait_for(
      test,
      [&handle, &query_calls]() {
        return query_calls >= 1 && handle->get_running_transactions().empty() &&
               handle->get_finished_transactions().empty();
      },
      std::chrono::milliseconds{4000}));

  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_GE(commit_ack_calls, 1);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_GE(recorder.count("on_resolve_task_finished"), 1);

  // 队列消费完毕后定时器已撤销（不再自驱动，也不会空转）
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());

  CASE_EXPECT_EQ(0, test.stop());
}

// 不变量：每个 handle 至多一个自定义定时器，且始终指向最先发生的事件；最早事件移除后重排到下一最早事件。
CASE_TEST(component_distributed_transaction_participator, custom_timer_points_at_earliest_event) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int commit_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  // A 先到期（20ms），B 后到（60s，用例内不会自然到期）
  auto task =
      test.run_task("earliest_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request_a = make_prepare_request("part-uuid-earliest-a", 2, std::chrono::milliseconds{20});
        auto request_b = make_prepare_request("part-uuid-earliest-b", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_a), response, output)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_b), response, output)));
        CASE_EXPECT_EQ(2, handle->get_running_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  auto& running_map = handle->get_running_transactions();
  auto iter_a = running_map.find("part-uuid-earliest-a");
  auto iter_b = running_map.find("part-uuid-earliest-b");
  CASE_EXPECT_TRUE(iter_a != running_map.end() && iter_b != running_map.end());
  if (iter_a == running_map.end() || iter_b == running_map.end()) {
    test.stop();
    return;
  }
  auto timepoint_a = protobuf_to_system_clock(iter_a->second.storage->resolve_timepoint());
  auto timepoint_b = protobuf_to_system_clock(iter_b->second.storage->resolve_timepoint());
  CASE_EXPECT_TRUE(timepoint_a < timepoint_b);

  // 两个事件在队列中，但只注册一个定时器且指向最早的 A
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_TRUE(handle->get_resolve_custom_timer_timepoint_for_unit_test() == timepoint_a);

  // 仅 pump：A 由定时器自驱动完成全流程；期间 B 不得被提前查询
  int query_calls_at_a_done = 0;
  CASE_EXPECT_TRUE(dt_test::wait_for(
      test,
      [&handle, &query_calls]() {
        return query_calls >= 1 && handle->get_running_transactions().count("part-uuid-earliest-a") == 0 &&
               handle->get_finished_transactions().empty();
      },
      std::chrono::milliseconds{4000}));
  query_calls_at_a_done = query_calls;
  CASE_EXPECT_GE(query_calls_at_a_done, 1);

  // B 仍在 running，且定时器已重排到 B 的到期时间点
  CASE_EXPECT_EQ(1, handle->get_running_transactions().count("part-uuid-earliest-b"));
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_TRUE(handle->get_resolve_custom_timer_timepoint_for_unit_test() == timepoint_b);

  // 外部 tick() 契约保留：手动推进 B 到期后全流程消费，队列清空时定时器撤销
  auto tick_context = atfw::testing::make_context();
  handle->tick(tick_context, timepoint_b + std::chrono::milliseconds{1});
  CASE_EXPECT_TRUE(dt_test::wait_for(
      test,
      [&handle, &query_calls, query_calls_at_a_done]() {
        return query_calls > query_calls_at_a_done && handle->get_running_transactions().empty() &&
               handle->get_finished_transactions().empty();
      },
      std::chrono::milliseconds{4000}));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(2, recorder.count("do_event"));

  CASE_EXPECT_EQ(0, test.stop());
}

// 回归：运行中的 resolve task 处理纯 acknowledge 批次且全部成功时不产生任何队列变更；
// 若此时另一事务的 resolve 定时器到期，on_resolve_custom_timer_fired 消费定时器后 tick() 因任务在途
// 提前返回、不重排定时器，队列中的 B 将永久失去驱动者（参与者没有外部周期 tick 兜底）。
// 任务收尾必须兜底重驱动队列，恢复“队列非空 ⇒ 存在驱动者（定时器或运行中的任务）”的不变量。
CASE_TEST(component_distributed_transaction_participator, custom_timer_redriven_when_fired_during_running_task) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  int commit_ack_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  // 首个 resolve task（ack A）拉起后停在 check_writable 闸门上，保证 B 的定时器在任务在途期间到期
  bool resolve_task_entered = false;
  bool release_resolve_task = false;
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  vtable->check_writable = [&resolve_task_entered, &release_resolve_task](rpc::context& ctx, handle_type&,
                                                                          bool& writable) -> rpc::result_code_type {
    resolve_task_entered = true;
    for (int i = 0; i < 5000 && !release_resolve_task; ++i) {
      RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
    }
    if (!release_resolve_task) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    }
    writable = true;
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  // A 经协调者 commit 通知转入 finished，产生立即到期的 acknowledge；B 保持 60s 的远期 deadline。
  auto task =
      test.run_task("redrive_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request_a = make_prepare_request("part-uuid-redrive-a", 2, std::chrono::milliseconds{60000});
        auto request_b = make_prepare_request("part-uuid-redrive-b", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_a), response, output)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_b), response, output)));

        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("part-uuid-redrive-a");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());

  auto finished_iter = handle->get_finished_transactions().find("part-uuid-redrive-a");
  auto running_iter = handle->get_running_transactions().find("part-uuid-redrive-b");
  CASE_EXPECT_TRUE(finished_iter != handle->get_finished_transactions().end());
  CASE_EXPECT_TRUE(running_iter != handle->get_running_transactions().end());
  if (finished_iter == handle->get_finished_transactions().end() ||
      running_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }

  // 精确触发 ack A 的存储 deadline，待 resolve task 确认进入闸门后，再精确触发 B 的 deadline。
  // 第二次触发会消费 B 的自定义定时器，但 tick() 因 task 在途而早退，稳定到达“定时器已丢”状态。
  handle->fire_resolve_custom_timer_for_unit_test(protobuf_to_system_clock(finished_iter->second->resolve_timepoint()));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&resolve_task_entered]() { return resolve_task_entered; }));
  handle->fire_resolve_custom_timer_for_unit_test(
      protobuf_to_system_clock(running_iter->second.storage->resolve_timepoint()));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(1, handle->get_running_transactions().count("part-uuid-redrive-b"));
  CASE_EXPECT_EQ(1, handle->get_finished_transactions().count("part-uuid-redrive-a"));

  // 放行 resolve task：ack A 成功（remove_finished 的队列 erase 为空操作，不产生队列变更，
  // 不会重新武装定时器）；把业务时钟推进到 B 的 deadline 后，任务收尾的兜底 tick 必须接管 B。
  // 运行时硬超时和协程调度仍使用原始系统时钟，case 不等待真实的 60s。
  {
    dt_test::global_now_offset_guard business_clock(std::chrono::seconds{61});
    release_resolve_task = true;
    CASE_EXPECT_TRUE(dt_test::wait_for(test, [&handle]() {
      return handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
    }));
  }

  // B 被收尾重驱动完整消费：query -> COMMITED -> do_event -> ack；A 与 B 的 ack 均已送达
  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_GE(commit_ack_calls, 2);
  CASE_EXPECT_EQ(2, recorder.count("do_event"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());

  CASE_EXPECT_EQ(0, test.stop());
}

// 安全性：handle 在定时器等待期间销毁时撤销定时器，之后不会有任何回调/任务/悬空访问
CASE_TEST(component_distributed_transaction_participator, custom_timer_cancelled_on_destroy) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  int query_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                                        &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  handle_type::resolve_custom_timer_watcher_for_unit_test_type registered_timer;
  {
    auto vtable = recorder.make_vtable();
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

    auto task = test.run_task(
        "destroy_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
          auto request = make_prepare_request("part-uuid-destroy", 2, std::chrono::milliseconds{30});
          SSParticipatorTransactionPrepareRsp response;
          storage_ptr_type output;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
          RPC_RETURN_CODE(0);
        });
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
    registered_timer = handle->get_resolve_custom_timer_watcher_for_unit_test();
    CASE_EXPECT_FALSE(registered_timer.expired());

    // 定时器仍在等待时销毁 handle：析构必须撤销定时器
    handle.reset();
  }

  // watcher 直接证明时间轮不再持有该定时器；无需等待真实 deadline，也不会受 CPU 调度影响。
  // prepare 本身会记录 on_start_running，故同时锁定析构没有产生新的生命周期回调。
  const size_t events_at_destroy = recorder.events.size();
  CASE_EXPECT_TRUE(registered_timer.expired());
  CASE_EXPECT_EQ(0, query_calls);
  CASE_EXPECT_EQ(events_at_destroy, recorder.events.size());

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ §4.3 补充：重复 prepare 幂等，不覆盖 storage/lock/timer，不重复触发生命周期事件 ============
CASE_TEST(component_distributed_transaction_participator, repeated_prepare_is_idempotent) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  int commit_ack_calls = 0;
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  // 第一次 prepare 建立 running 条目；第二次同 UUID prepare（不同的 lock 集合）幂等返回同一对象
  auto task = test.run_task(
      "repeat_prepare_running", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request_1 = make_prepare_request("part-uuid-repeat", 3, std::chrono::milliseconds{60000}, {"res-a"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output_1;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_1), response, output_1)));
        CASE_EXPECT_TRUE(!!output_1);
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        CASE_EXPECT_EQ(output_1.get(), handle->get_locker("res-a").get());

        auto request_2 = make_prepare_request("part-uuid-repeat", 3, std::chrono::milliseconds{60000}, {"res-b"});
        storage_ptr_type output_2;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_2), response, output_2)));
        // 返回现有对象：第二次 prepare 的 storage（res-b）不覆盖第一次的内容
        CASE_EXPECT_EQ(output_1.get(), output_2.get());
        CASE_EXPECT_EQ(1, output_1->lock_resource_size());
        CASE_EXPECT_EQ(std::string("res-a"), output_1->lock_resource(0));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-b"));
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // 生命周期事件不重复触发（check_prepare 是每次 prepare 都会执行的业务校验门，预期重新执行）
  CASE_EXPECT_EQ(1, recorder.count("on_start_running"));
  CASE_EXPECT_EQ(2, recorder.count("check_prepare"));
  // 定时器仍指向首个到期事件的到期时间点，未被重复 prepare 重置
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  auto repeat_iter = handle->get_running_transactions().find("part-uuid-repeat");
  CASE_EXPECT_TRUE(repeat_iter != handle->get_running_transactions().end());
  if (repeat_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(protobuf_to_system_clock(repeat_iter->second.storage->resolve_timepoint()),
                 handle->get_resolve_custom_timer_timepoint_for_unit_test());

  // commit 后进入 finished；同 UUID 再次 prepare 返回 finished 副本而不重建 running 条目
  auto commit_task = test.run_task(
      "repeat_prepare_finished", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("part-uuid-repeat");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());

        auto request_3 = make_prepare_request("part-uuid-repeat", 3, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output_3;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_3), response, output_3)));
        CASE_EXPECT_TRUE(!!output_3);
        // 同一 finished 对象，状态保持终局，不允许重新进入 running（防止同一 UUID 再次执行 do_event）
        auto finished_iter = handle->get_finished_transactions().find("part-uuid-repeat");
        CASE_EXPECT_TRUE(finished_iter != handle->get_finished_transactions().end());
        if (finished_iter == handle->get_finished_transactions().end()) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(finished_iter->second.get(), output_3.get());
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                       output_3->metadata().status());
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        RPC_RETURN_CODE(0);
      });
  auto commit_result = test.wait(commit_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(commit_result.task_exited);
  CASE_EXPECT_EQ(0, commit_result.result_code);
  CASE_EXPECT_EQ(1, recorder.count("on_start_running"));
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_EQ(3, recorder.count("check_prepare"));

  // 收尾：驱动 acknowledge 至完成，保持资源卫生
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle]() { return handle->get_finished_transactions().empty(); }));
  CASE_EXPECT_TRUE(commit_ack_calls >= 1);
  CASE_EXPECT_EQ(0, test.stop());
}

// ============ §4.3 补充：批次中途失去可写性时，剩余 pending 条目延后且按退避重排 ============
CASE_TEST(component_distributed_transaction_participator, writable_loss_mid_batch_defers_remaining) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  // 记录每次 query 的事务 UUID，响应 COMMITED
  std::vector<std::string> queried_uuids;
  auto query_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      atfw::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [&queried_uuids](const atfw::testing::ss_request_view& request,
                       google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionQueryReq&>(request.body);
        queried_uuids.push_back(typed_request.metadata().transaction_uuid());
        auto& typed_response = static_cast<atfw::distributed_system::SSDistributeTransactionQueryRsp&>(response);
        auto* query_storage = typed_response.mutable_storage();
        protobuf_copy_message(*query_storage->mutable_metadata(), typed_request.metadata());
        query_storage->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        auto& mock_participator = (*query_storage->mutable_participators())[std::string{"p"}];
        mock_participator.set_participator_key("p");
        mock_participator.set_participator_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  int commit_ack_calls = 0;
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  // 脚本：批次闸门可写、A 可写、B 不可写（批次中途失去可写性）；脚本耗尽后默认不可写，
  // 使后续自驱动周期持续延后全部剩余条目
  recorder.check_writable_result = false;
  recorder.check_writable_values = {1, 1, 0};

  // 两个 20ms 到期的事务（到期时间相同，pending 顺序按 UUID tie-break：A 先于 B）
  auto task = test.run_task(
      "writable_batch_seed", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request_a = make_prepare_request("part-uuid-wb-a", 2, std::chrono::milliseconds{20});
        auto request_b = make_prepare_request("part-uuid-wb-b", 2, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_a), response, output)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_b), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // 纯自驱动：A 完成 query → do_event；B 在批次中途被跳过并随重试退避重新排期；
  // 等待至少一个后续延后周期结束，确认状态稳定（不再消费任何条目）
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") >= 2; }));
  CASE_EXPECT_EQ(1, queried_uuids.size());
  CASE_EXPECT_EQ(std::string("part-uuid-wb-a"), queried_uuids[0]);
  CASE_EXPECT_EQ(0, handle->get_running_transactions().count("part-uuid-wb-a"));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().count("part-uuid-wb-b"));
  // 延后不消耗 B 的 resolve 预算
  auto wb_b_iter = handle->get_running_transactions().find("part-uuid-wb-b");
  CASE_EXPECT_TRUE(wb_b_iter != handle->get_running_transactions().end());
  if (wb_b_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(0, wb_b_iter->second.storage->resolve_times());
  // 批次闸门保持不可写时，A 的 acknowledge 也一并延后
  CASE_EXPECT_EQ(1, handle->get_finished_transactions().count("part-uuid-wb-a"));
  CASE_EXPECT_EQ(1, recorder.count("do_event"));

  // 恢复可写性：定时器自驱动消费全部剩余条目（A 的 acknowledge + B 的 resolve）
  recorder.check_writable_result = true;
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&handle]() {
    return handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_EQ(2, queried_uuids.size());
  CASE_EXPECT_EQ(std::string("part-uuid-wb-b"), queried_uuids[1]);
  CASE_EXPECT_TRUE(commit_ack_calls >= 2);
  CASE_EXPECT_EQ(2, recorder.count("do_event"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());

  CASE_EXPECT_EQ(0, test.stop());
}
