// Copyright 2026 atframework
//
// transaction_participator_handle unit tests (UNIT_TEST_EXECUTION_PLAN.md section 4.3): prepare
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

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "dt_test_common.h"
#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"
#include "rpc/transaction/transaction_api.h"
#include "transaction_participator_handle.h"

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
  std::vector<int32_t> do_event_script;     // consumed FIFO; empty = always success
  std::vector<int32_t> undo_event_script;
  std::vector<int32_t> check_prepare_script;
  std::vector<int32_t> check_writable_script;
  bool check_prepare_allow_retry = false;
  bool check_writable_result = true;
  int undo_calls = 0;

  atfw::util::memory::strong_rc_ptr<handle_type::vtable_type> make_vtable() {
    auto vtable = atfw::component::memory::stl::make_strong_rc<handle_type::vtable_type>();
    vtable->do_event = [this](rpc::context&, handle_type&,
                              const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("do_event");
      int32_t res = pop(do_event_script);
      RPC_RETURN_CODE(res);
    };
    vtable->undo_event = [this](rpc::context&, handle_type&,
                                const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("undo_event");
      ++undo_calls;
      int32_t res = pop(undo_event_script);
      RPC_RETURN_CODE(res);
    };
    vtable->check_prepare = [this](rpc::context&, handle_type&, handle_type::storage_type& storage,
                                   atframework::distributed_system::transaction_participator_failure_reason& reason)
                                   -> rpc::result_code_type {
      events.push_back("check_prepare");
      if (check_prepare_allow_retry) {
        reason.set_allow_retry(true);
      }
      RPC_RETURN_CODE(pop(check_prepare_script));
    };
    vtable->check_writable = [this](rpc::context&, handle_type&, bool& writable) -> rpc::result_code_type {
      writable = check_writable_result;
      RPC_RETURN_CODE(pop(check_writable_script));
    };
    vtable->on_start_running = [this](rpc::context&, handle_type&,
                                      const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("on_start_running");
      RPC_RETURN_CODE(0);
    };
    vtable->on_finish_running = [this](rpc::context&, handle_type&,
                                       const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("on_finish_running");
      RPC_RETURN_CODE(0);
    };
    vtable->on_commited = [this](rpc::context&, handle_type&,
                                 const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("on_commited");
      RPC_RETURN_CODE(0);
    };
    vtable->on_rejected = [this](rpc::context&, handle_type&,
                                 const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("on_rejected");
      RPC_RETURN_CODE(0);
    };
    vtable->on_finished = [this](rpc::context&, handle_type&,
                                 const handle_type::storage_type&) -> rpc::result_code_type {
      events.push_back("on_finished");
      RPC_RETURN_CODE(0);
    };
    vtable->on_resolve_task_finished = [this](rpc::context&, handle_type&) -> rpc::result_code_type {
      events.push_back("on_resolve_task_finished");
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
SSParticipatorTransactionPrepareReq make_prepare_request(gsl::string_view uuid, uint32_t resolve_max_times = 3,
                                                         std::chrono::milliseconds expire_in =
                                                             std::chrono::milliseconds{20},
                                                         const std::vector<std::string>& lock_resources = {},
                                                         uint32_t resolve_times = 0) {
  SSParticipatorTransactionPrepareReq request;
  auto* storage = request.mutable_storage();
  storage->mutable_metadata()->set_transaction_uuid(uuid.data(), uuid.size());
  storage->mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  auto now = std::chrono::system_clock::now();
  auto* prepare_timepoint = storage->mutable_metadata()->mutable_prepare_timepoint();
  prepare_timepoint->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
  prepare_timepoint->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() % 1000000000);
  auto expire = now + expire_in;
  auto* expire_timepoint = storage->mutable_metadata()->mutable_expire_timepoint();
  expire_timepoint->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(expire.time_since_epoch()).count());
  expire_timepoint->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(expire.time_since_epoch()).count() % 1000000000);

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
        auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionQueryReq&>(request.body);
        auto& typed_response = static_cast<atfw::distributed_system::SSDistributeTransactionQueryRsp&>(response);
        auto* query_storage = typed_response.mutable_storage();
        protobuf_copy_message(*query_storage->mutable_metadata(), typed_request.metadata());
        query_storage->mutable_metadata()->set_status(terminal);
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
  gsl::string_view rpc_name =
      is_commit ? rpc::transaction::packer::get_full_name_of_commit_participator()
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
      [call_count, fail_script, is_commit](const atfw::testing::ss_request_view& request,
                                           google::protobuf::Message& response) -> rpc::result_code_type {
        if (nullptr != call_count) {
          ++*call_count;
        }
        if (nullptr != fail_script && !fail_script->empty()) {
          int32_t res = fail_script->front();
          fail_script->erase(fail_script->begin());
          RPC_RETURN_CODE(res);
        }
        auto& typed_request = static_cast<const google::protobuf::Message&>(request.body);
        auto& typed_response = static_cast<google::protobuf::Message&>(response);
        // Fill the metadata field of the typed response via reflection (field name "metadata").
        const auto* descriptor = typed_request.GetDescriptor();
        const auto* request_metadata_field = descriptor->FindFieldByName("metadata");
        const auto* response_metadata_field = typed_response.GetDescriptor()->FindFieldByName("metadata");
        if (nullptr != request_metadata_field && nullptr != response_metadata_field) {
          const auto& metadata = typed_request.GetReflection()->GetMessage(typed_request, request_metadata_field);
          auto* response_metadata = typed_response.GetReflection()->MutableMessage(&typed_response, response_metadata_field);
          response_metadata->CopyFrom(metadata);
        }
        RPC_RETURN_CODE(0);
      });
}

// Drives the resolve timers of a handle until pred() is true (ticks with the real clock and pumps
// the runtime in between).
bool drive_handle(atfw::testing::runtime& test, const atfw::util::memory::strong_rc_ptr<handle_type>& handle,
                  const std::function<bool()>& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds{4000}) {
  auto tick_context = atfw::testing::make_context();
  return dt_test::wait_for(
      test,
      [&handle, &tick_context, &pred]() {
        handle->tick(tick_context, std::chrono::system_clock::now());
        return pred();
      },
      timeout);
}

void set_prepare_timepoint(handle_type& handle, const std::string& uuid, int64_t seconds, int32_t nanos) {
  auto& running = const_cast<std::unordered_map<std::string, handle_type::running_transaction_entry>&>(
      handle.get_running_transactions());
  auto iter = running.find(uuid);
  if (iter == running.end() || !iter->second.storage) {
    return;
  }
  iter->second.storage->mutable_metadata()->mutable_prepare_timepoint()->set_seconds(seconds);
  iter->second.storage->mutable_metadata()->mutable_prepare_timepoint()->set_nanos(nanos);
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

  auto task = test.run_task("prepare_non_arena", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
    arena_storage->mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
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
  auto guard_task = test.run_task("prepare_empty_uuid", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    SSParticipatorTransactionPrepareReq empty_request;
    SSParticipatorTransactionPrepareRsp empty_response;
    storage_ptr_type empty_output;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
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

  auto task = test.run_task("prepare_allow_retry", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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

  auto task = test.run_task("commit_lifecycle", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"check_prepare", "on_start_running", "do_event", "on_finish_running", "on_finished", "on_commited"}));

  // force_commit prepare path: completes inside prepare with no running/finished state.
  recorder.events.clear();
  auto force_task = test.run_task("force_commit_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"check_prepare", "on_start_running", "do_event", "on_finish_running", "on_finished", "on_commited"}));

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

  auto task = test.run_task("force_commit_failure", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("lock_wound", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    auto older_request = make_prepare_request("part-uuid-older", 2, std::chrono::milliseconds{60000});
    auto younger_request = make_prepare_request("part-uuid-younger", 2, std::chrono::milliseconds{60000});
    SSParticipatorTransactionPrepareRsp response;
    storage_ptr_type older_output;
    storage_ptr_type younger_output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(older_request), response, older_output)));
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(younger_request), response, younger_output)));

    // Explicit timestamps: older transaction was prepared earlier.
    set_prepare_timepoint(*handle, "part-uuid-older", 1000, 0);
    set_prepare_timepoint(*handle, "part-uuid-younger", 2000, 0);

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
        younger_rejecting =
            running.metadata().status() == EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING;
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
  auto reload_task = test.run_task("wound_reload", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    atframework::distributed_system::transaction_participator_snapshot snapshot;
    handle->dump(snapshot);
    auto reloaded = atfw::component::memory::stl::make_strong_rc<handle_type>(
        participator_event_recorder{}.make_vtable(), "p2");
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

  auto task = test.run_task("appended_locks", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
    // storage.lock_resource and lock map are cleared together
    auto& running = const_cast<std::unordered_map<std::string, handle_type::running_transaction_entry>&>(
        handle.get()->get_running_transactions());
    CASE_EXPECT_EQ(0, running.at("part-uuid-locks").storage->lock_resource_size());

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

  auto task = test.run_task("check_lock", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    auto request = make_prepare_request("part-uuid-holder", 2, std::chrono::milliseconds{60000}, {"res-1", "res-2"});
    SSParticipatorTransactionPrepareRsp response;
    storage_ptr_type output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
    set_prepare_timepoint(*handle, "part-uuid-holder", 1000, 100);

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

  auto task = test.run_task("load_dump", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    // One running (with locks) and one finished transaction.
    auto running_request = make_prepare_request("part-uuid-running", 4, std::chrono::milliseconds{60000}, {"res-1"});
    SSParticipatorTransactionPrepareRsp response;
    storage_ptr_type running_output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(running_request), response, running_output)));

    auto finished_request = make_prepare_request("part-uuid-finished", 4, std::chrono::milliseconds{60000});
    storage_ptr_type finished_output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(finished_request), response, finished_output)));
    SSParticipatorTransactionCommitReq commit_request;
    commit_request.set_transaction_uuid("part-uuid-finished");
    SSParticipatorTransactionCommitRsp commit_response;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));

    atframework::distributed_system::transaction_participator_snapshot snapshot;
    handle->dump(snapshot);
    CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
    CASE_EXPECT_EQ(1, snapshot.finished_transaction_size());

    // load replaces the state instead of appending.
    auto reloaded = atfw::component::memory::stl::make_strong_rc<handle_type>(
        participator_event_recorder{}.make_vtable(), "p2");
    auto stale_request = make_prepare_request("part-uuid-stale");
    SSParticipatorTransactionPrepareRsp stale_response;
    storage_ptr_type stale_output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(reloaded->prepare(ctx, std::move(stale_request), stale_response, stale_output)));
    CASE_EXPECT_EQ(1, reloaded->get_running_transactions().size());

    reloaded->load(snapshot);
    CASE_EXPECT_EQ(1, reloaded->get_running_transactions().size());
    CASE_EXPECT_EQ(1, reloaded->get_finished_transactions().size());
    CASE_EXPECT_EQ(0, reloaded->get_running_transactions().count("part-uuid-stale"));
    CASE_EXPECT_TRUE(!!reloaded->get_locker("res-1"));
    CASE_EXPECT_EQ(std::string("part-uuid-running"),
                   reloaded->get_locker("res-1")->metadata().transaction_uuid());

    // Duplicate running entries in one snapshot keep the first occurrence.
    auto* duplicate = snapshot.add_running_transaction();
    duplicate->mutable_metadata()->set_transaction_uuid("part-uuid-running");
    duplicate->mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
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
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED, &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("resolve_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
    return query_calls >= 1 && handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
  }));

  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_GE(commit_ack_calls, 1);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_GE(recorder.count("on_resolve_task_finished"), 1);

  // Full local consumption: running/finished/dump/locks are all empty.
  atframework::distributed_system::transaction_participator_snapshot snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.running_transaction_size() + snapshot.finished_transaction_size());

  // Further ticks do nothing (no tight loop).
  auto tick_context = atfw::testing::make_context();
  for (int i = 0; i < 8; ++i) {
    handle->tick(tick_context, std::chrono::system_clock::now() + std::chrono::hours{1});
    test.pump_once();
  }
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
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED, &query_calls);
  auto commit_ack_rule = register_participator_ack_mock(test, "commit", &commit_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  recorder.do_event_script = {-1, -1, -1, -1, -1, -1, -1, -1};  // always fails
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("action_failure_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED, &query_calls);
  auto reject_ack_rule = register_participator_ack_mock(test, "reject", &reject_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!reject_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("budget_exhaustion", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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

  auto task = test.run_task("notfound_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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

  auto task = test.run_task("ack_budget_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED, &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  recorder.check_writable_result = false;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("writable_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    bool writable = true;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->check_writable(ctx, writable)));
    CASE_EXPECT_FALSE(writable);

    auto request = make_prepare_request("part-uuid-readonly", 2, std::chrono::milliseconds{20});
    SSParticipatorTransactionPrepareRsp response;
    storage_ptr_type output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  // Due timers fire but the resolve task defers everything: no query, no consumption.
  auto tick_context = atfw::testing::make_context();
  for (int i = 0; i < 30; ++i) {
    handle->tick(tick_context, std::chrono::system_clock::now());
    test.pump_once();
  }
  CASE_EXPECT_EQ(0, query_calls);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().size());

  // Recovering writability resumes the flow.
  recorder.check_writable_result = true;
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &query_calls]() {
    return query_calls >= 1 && handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
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

  auto task = test.run_task("check_prepare_error", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED, &query_calls);
  auto reject_ack_rule = register_participator_ack_mock(test, "reject", &reject_ack_calls);
  CASE_EXPECT_TRUE(!!query_rule && !!reject_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("resolve_rejected_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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
  auto query_rule =
      register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED, &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  auto task = test.run_task("resolve_prepared_prepare", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
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

  auto task = test.run_task("check_lock_multi", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    auto request = make_prepare_request("part-uuid-holder-multi", 2, std::chrono::milliseconds{60000}, {"res-1", "res-2"});
    SSParticipatorTransactionPrepareRsp response;
    storage_ptr_type output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
    set_prepare_timepoint(*handle, "part-uuid-holder-multi", 1000, 0);

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
