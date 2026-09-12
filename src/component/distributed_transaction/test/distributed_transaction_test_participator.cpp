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

#include <algorithm>
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

rpc::result_code_type prepare_wound(rpc::context& ctx, const atfw::util::memory::strong_rc_ptr<handle_type>& handle,
                                    storage_ptr_type& older, storage_ptr_type& younger, bool replicated = false,
                                    uint32_t retry_limit = 2) {
  auto older_request = make_prepare_request("wound-older", 2, std::chrono::minutes{20}, {"res-older"});
  auto younger_request =
      make_prepare_request("wound-younger", retry_limit, std::chrono::minutes{10}, {"res-younger", "res-extra"});
  *older_request.mutable_storage()->mutable_metadata()->mutable_prepare_timepoint() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() - std::chrono::seconds{1});
  younger_request.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval()->set_seconds(60);
  if (replicated) {
    auto* metadata = younger_request.mutable_storage()->mutable_metadata();
    metadata->set_replicate_read_count(2);
    metadata->add_replicate_node_server_id(0x1B0001);
    metadata->add_replicate_node_server_id(0x1B0002);
    metadata->add_replicate_node_server_id(0x1B0003);
  }
  SSParticipatorTransactionPrepareRsp response;
  CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(older_request), response, older)));
  CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(younger_request), response, younger)));
  if (!older || !younger) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  google::protobuf::RepeatedPtrField<std::string> resources;
  resources.Add()->assign("res-older");
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                 RPC_AWAIT_CODE_RESULT(handle->lock(younger, resources)));
  CASE_EXPECT_FALSE(handle->get_running_transactions().at("wound-older").wounded);
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED, older->metadata().status());
  resources.Clear();
  resources.Add()->assign("res-free");
  resources.Add()->assign("res-younger");
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                 RPC_AWAIT_CODE_RESULT(handle->lock(older, resources)));
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                 younger->metadata().status());
  CASE_EXPECT_TRUE(handle->get_running_transactions().at("wound-younger").wounded);
  CASE_EXPECT_EQ(older.get(), handle->get_locker("res-older").get());
  CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-younger").get());
  CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-extra").get());
  CASE_EXPECT_FALSE(!!handle->get_locker("res-free"));
  CASE_EXPECT_EQ(1, older->lock_resource_size());
  CASE_EXPECT_EQ(2, younger->lock_resource_size());
  CASE_EXPECT_EQ(protobuf_to_system_clock(younger->metadata().expire_timepoint()),
                 protobuf_to_system_clock(younger->resolve_timepoint()));
  RPC_RETURN_CODE(0);
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
void run_normal_state_lifecycle(bool commit, bool with_do_event, bool exhaust_finished) {
  const auto pending_status = commit ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING
                                     : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING;
  const auto terminal_status = commit ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED
                                      : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  int ack_calls = 0;
  std::vector<int32_t> ack_errors = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto ack_rule = register_participator_ack_mock(test, commit ? "commit" : "reject", &ack_calls, &ack_errors);
  CASE_EXPECT_TRUE(!!ack_rule);
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  if (with_do_event) {
    vtable->do_event = [&recorder, pending_status](rpc::context&, handle_type&,
                                                   const handle_type::storage_type& storage) -> rpc::result_code_type {
      CASE_EXPECT_EQ(pending_status, storage.metadata().status());
      recorder.events.emplace_back("do_event");
      RPC_RETURN_CODE(recorder.count("do_event") == 1 ? PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT : 0);
    };
  } else {
    vtable->do_event = nullptr;
  }
  vtable->on_start_running = [&recorder](rpc::context&, handle_type&,
                                         const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                   storage.metadata().status());
    recorder.events.emplace_back("on_start_running");
    RPC_RETURN_CODE(0);
  };
  vtable->on_finish_running = [&recorder, pending_status](
                                  rpc::context&, handle_type& owner,
                                  const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(pending_status, storage.metadata().status());
    CASE_EXPECT_TRUE(owner.get_running_transactions().empty());
    CASE_EXPECT_EQ(1, owner.get_finished_transactions().size());
    CASE_EXPECT_FALSE(!!owner.get_locker("state-resource"));
    recorder.events.emplace_back("on_finish_running");
    RPC_RETURN_CODE(0);
  };
  bool finish_entered = false;
  bool release_finish = false;
  int finish_calls = 0;
  vtable->on_finished = [&finish_entered, &release_finish, &finish_calls, &ack_calls, pending_status, exhaust_finished](
                            rpc::context& ctx, handle_type&,
                            const handle_type::storage_type& storage) -> rpc::result_code_type {
    ++finish_calls;
    CASE_EXPECT_EQ(pending_status, storage.metadata().status());
    CASE_EXPECT_EQ(0, ack_calls);
    if (finish_calls == 1) {
      finish_entered = true;
      while (!release_finish) {
        auto res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }
      }
    }
    CASE_EXPECT_EQ(pending_status, storage.metadata().status());
    RPC_RETURN_CODE(finish_calls == 1 || exhaust_finished ? PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT : 0);
  };
  auto terminal_callback = [&recorder, pending_status, commit](
                               rpc::context&, handle_type&,
                               const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(pending_status, storage.metadata().status());
    recorder.events.emplace_back(commit ? "on_commited" : "on_rejected");
    RPC_RETURN_CODE(0);
  };
  if (commit) {
    vtable->on_commited = terminal_callback;
  } else {
    vtable->on_rejected = terminal_callback;
  }
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type output;
  auto task = test.run_task(
      "normal_state_lifecycle", std::chrono::seconds{4},
      [handle, &output, commit, with_do_event](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("state-lifecycle", 2, std::chrono::milliseconds{60000}, {"state-resource"});
        request.mutable_storage()->mutable_metadata()->clear_status();
        request.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval()->set_seconds(60);
        SSParticipatorTransactionPrepareRsp response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                       output->metadata().status());
        if (commit) {
          SSParticipatorTransactionCommitReq commit_request;
          commit_request.set_transaction_uuid("state-lifecycle");
          SSParticipatorTransactionCommitRsp commit_response;
          if (with_do_event) {
            CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                           RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
            CASE_EXPECT_TRUE(!!handle->get_locker("state-resource"));
            handle_type::snapshot_type snapshot;
            handle->dump(snapshot);
            if (!CASE_EXPECT_EQ(1, snapshot.running_transaction_size())) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
            }
            CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                           snapshot.running_transaction(0).metadata().status());
            CASE_EXPECT_EQ(1, snapshot.running_transaction(0).resolve_times());
            handle->load(snapshot);
            output = handle->get_locker("state-resource");
          }
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        }
        SSParticipatorTransactionRejectReq reject_request;
        reject_request.set_transaction_uuid("state-lifecycle");
        SSParticipatorTransactionRejectRsp reject_response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
      });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&finish_entered]() { return finish_entered; }));
  auto probe = test.run_task(
      "probe_pending_finish", std::chrono::seconds{2}, [handle, commit](rpc::context& ctx) -> rpc::result_code_type {
        if (!commit) {
          SSParticipatorTransactionCommitReq request;
          SSParticipatorTransactionCommitRsp response;
          request.set_transaction_uuid("state-lifecycle");
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, request, response)));
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                         handle->get_finished_transactions().at("state-lifecycle")->metadata().status());
        }
        CASE_EXPECT_EQ(0, handle->tick(ctx, std::chrono::system_clock::now() + std::chrono::hours{1}));
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1})));
      });
  auto probe_result = test.wait(probe, std::chrono::seconds{3});
  CASE_EXPECT_TRUE(probe_result.task_exited);
  CASE_EXPECT_EQ(0, probe_result.result_code);
  CASE_EXPECT_EQ(0, ack_calls);
  release_finish = true;
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, result.result_code);
  handle_type::snapshot_type snapshot;
  handle->dump(snapshot);
  if (!CASE_EXPECT_EQ(1, snapshot.finished_transaction_size())) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  CASE_EXPECT_EQ(pending_status, snapshot.finished_transaction(0).metadata().status());
  CASE_EXPECT_EQ(1, snapshot.finished_transaction(0).resolve_times());
  CASE_EXPECT_FALSE(snapshot.finished_transaction(0).finished_callback_completed());
  handle->load(snapshot);
  auto current = handle->get_finished_transactions().find("state-lifecycle");
  if (!CASE_EXPECT_TRUE(current != handle->get_finished_transactions().end())) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  output = current->second;
  if (!exhaust_finished) {
    CASE_EXPECT_TRUE(
        drive_handle(test, handle, [&ack_calls, &output]() { return ack_calls == 1 && output->resolve_times() == 1; }));
    CASE_EXPECT_EQ(pending_status, output->metadata().status());
    CASE_EXPECT_EQ(2, finish_calls);
    CASE_EXPECT_TRUE(output->finished_callback_completed());
    handle->dump(snapshot);
    handle->load(snapshot);
    current = handle->get_finished_transactions().find("state-lifecycle");
    if (!CASE_EXPECT_TRUE(current != handle->get_finished_transactions().end())) {
      CASE_EXPECT_EQ(0, test.stop());
      return;
    }
    output = current->second;
    CASE_EXPECT_EQ(pending_status, output->metadata().status());
  }
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return handle->get_finished_transactions().empty(); }));
  CASE_EXPECT_EQ(2, finish_calls);
  CASE_EXPECT_EQ(exhaust_finished ? pending_status : terminal_status, output->metadata().status());
  CASE_EXPECT_EQ(exhaust_finished ? 0 : 2, ack_calls);
  CASE_EXPECT_EQ(commit && with_do_event ? 2 : 0, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_finish_running"));
  CASE_EXPECT_EQ(exhaust_finished ? 0 : 1, recorder.count(commit ? "on_commited" : "on_rejected"));
  CASE_EXPECT_FALSE(!!handle->get_locker("state-resource"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(0, test.stop());
}

static void run_snapshot_ack_lifecycle(EnDistibutedTransactionStatus status, bool finished) {
  const bool commit = status == EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING ||
                      status == EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED;
  const auto terminal_status = commit ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED
                                      : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  int ack_calls = 0;
  std::vector<int32_t> ack_errors = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto ack_rule = register_participator_ack_mock(test, commit ? "commit" : "reject", &ack_calls, &ack_errors);
  CASE_EXPECT_TRUE(!!ack_rule);
  int global_reject_calls = 0;
  auto reject_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject(),
      atfw::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionRejectRsp::descriptor()->full_name(),
      [&global_reject_calls](const atfw::testing::ss_request_view& request,
                             google::protobuf::Message& response) -> rpc::result_code_type {
        ++global_reject_calls;
        const auto& input =
            static_cast<const atfw::distributed_system::SSDistributeTransactionRejectReq&>(request.body);
        auto& output = static_cast<atfw::distributed_system::SSDistributeTransactionRejectRsp&>(response);
        *output.mutable_metadata() = input.metadata();
        output.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!reject_rule);
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  vtable->do_event = [&recorder, status](rpc::context&, handle_type&,
                                         const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(status, storage.metadata().status());
    recorder.events.emplace_back("do_event");
    RPC_RETURN_CODE(0);
  };
  vtable->on_finished = [&recorder, status](rpc::context&, handle_type&,
                                            const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(status, storage.metadata().status());
    recorder.events.emplace_back("on_finished");
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  auto request = make_prepare_request("snapshot-ack", 2, std::chrono::milliseconds{60000});
  request.mutable_storage()->mutable_metadata()->set_status(status);
  request.mutable_storage()->set_finished_callback_completed(finished);
  protobuf_copy_message(*request.mutable_storage()->mutable_resolve_timepoint(),
                        request.storage().metadata().expire_timepoint());
  // running represents pending local work; finished represents work already awaiting ACK.
  handle_type::snapshot_type snapshot;
  protobuf_copy_message(finished ? *snapshot.add_finished_transaction() : *snapshot.add_running_transaction(),
                        request.storage());
  handle->load(snapshot);
  handle_type::snapshot_type restored;
  handle->dump(restored);
  CASE_EXPECT_EQ(snapshot.SerializeAsString(), restored.SerializeAsString());
  storage_ptr_type output = finished ? handle->get_finished_transactions().at("snapshot-ack")
                                     : handle->get_running_transactions().at("snapshot-ack").storage;
  if (!commit) {
    auto task = test.run_task("commit_after_reject_snapshot", std::chrono::seconds{2},
                              [handle, finished, output, status](rpc::context& ctx) -> rpc::result_code_type {
                                SSParticipatorTransactionCommitReq request;
                                SSParticipatorTransactionCommitRsp response;
                                request.set_transaction_uuid("snapshot-ack");
                                CASE_EXPECT_EQ(finished ? 0 : PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED,
                                               RPC_AWAIT_CODE_RESULT(handle->commit(ctx, request, response)));
                                CASE_EXPECT_EQ(status, output->metadata().status());
                                RPC_RETURN_CODE(0);
                              });
    auto result = test.wait(task, std::chrono::seconds{3});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_EQ(0, recorder.count("do_event"));
  }
  CASE_EXPECT_TRUE(
      drive_handle(test, handle, [&ack_calls, &output]() { return ack_calls == 1 && output->resolve_times() == 1; }));
  CASE_EXPECT_EQ(status, output->metadata().status());
  CASE_EXPECT_TRUE(output->finished_callback_completed());
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
  handle->dump(snapshot);
  handle->load(snapshot);
  auto current = handle->get_finished_transactions().find("snapshot-ack");
  if (!CASE_EXPECT_TRUE(current != handle->get_finished_transactions().end())) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  output = current->second;
  CASE_EXPECT_EQ(status, output->metadata().status());
  CASE_EXPECT_EQ(1, output->resolve_times());
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return handle->get_finished_transactions().empty(); }));
  CASE_EXPECT_EQ(terminal_status, output->metadata().status());
  CASE_EXPECT_EQ(2, ack_calls);
  CASE_EXPECT_EQ(0, global_reject_calls);
  CASE_EXPECT_EQ(!finished && commit ? 1 : 0, recorder.count("do_event"));
  CASE_EXPECT_EQ(finished ? 0 : 1, recorder.count("on_finished"));
  CASE_EXPECT_EQ(finished ? 0 : 1, recorder.count(commit ? "on_commited" : "on_rejected"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(0, test.stop());
}
void run_wound_decision(bool replicated, bool committed) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  const std::vector<uint64_t> nodes =
      replicated ? std::vector<uint64_t>{0x1B0001, 0x1B0002, 0x1B0003} : std::vector<uint64_t>{0x1B0001};
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, nodes));
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto original_do = vtable->do_event;
  vtable->do_event = [original_do](rpc::context& ctx, handle_type& owner,
                                   const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(&storage, owner.get_locker("res-younger").get());
    CASE_EXPECT_EQ(&storage, owner.get_locker("res-extra").get());
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage.metadata().status());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(original_do(ctx, owner, storage)));
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type older;
  storage_ptr_type younger;
  auto prepare_task =
      test.run_task("prepare_wound", std::chrono::seconds{4},
                    [handle, &older, &younger, replicated](rpc::context& ctx) -> rpc::result_code_type {
                      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(prepare_wound(ctx, handle, older, younger, replicated, 4)));
                    });
  auto prepare_result = test.wait(prepare_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(prepare_result.task_exited);
  CASE_EXPECT_EQ(0, prepare_result.result_code);
  if (!younger) {
    test.stop();
    return;
  }
  int32_t decision_node_error = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  int global_reject_calls = 0;
  std::vector<atfw::testing::ss_rule_handle> rules;
  for (uint64_t node : nodes) {
    const bool is_decision_node = node == (replicated ? 0x1B0002ULL : 0x1B0001ULL);
    const bool is_unavailable_node = replicated && node == 0x1B0003;
    atfw::testing::ss_rule_options rule_options;
    rule_options.match_node_id = node;
    rule_options.delay_generations = is_decision_node ? 3 : 0;
    rules.emplace_back(test.ss().mock(
        rpc::transaction::packer::get_full_name_of_reject(),
        atfw::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name(),
        atfw::distributed_system::SSDistributeTransactionRejectRsp::descriptor()->full_name(),
        [handle, &younger, &recorder, &decision_node_error, &global_reject_calls, is_decision_node, is_unavailable_node,
         committed, replicated](const atfw::testing::ss_request_view& request,
                                google::protobuf::Message& response) -> rpc::result_code_type {
          ++global_reject_calls;
          CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-younger").get());
          CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-extra").get());
          CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
          const auto& in = static_cast<const atfw::distributed_system::SSDistributeTransactionRejectReq&>(request.body);
          CASE_EXPECT_EQ(std::string("wound-younger"), in.metadata().transaction_uuid());
          CASE_EXPECT_EQ(replicated ? 2 : 0, in.metadata().replicate_read_count());
          if (is_unavailable_node) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
          }
          if (is_decision_node && decision_node_error < 0) {
            RPC_RETURN_CODE(decision_node_error);
          }
          auto& out = static_cast<atfw::distributed_system::SSDistributeTransactionRejectRsp&>(response);
          *out.mutable_metadata() = in.metadata();
          out.mutable_metadata()->set_status(
              committed && is_decision_node
                  ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED
                  : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
          RPC_RETURN_CODE(0);
        },
        rule_options));
    CASE_EXPECT_TRUE(!!rules.back());
  }
  int commit_acks = 0;
  int reject_acks = 0;
  auto commit_rule = register_participator_ack_mock(test, "commit", &commit_acks);
  auto reject_rule = register_participator_ack_mock(test, "reject", &reject_acks);
  CASE_EXPECT_TRUE(!!commit_rule && !!reject_rule);
  const int32_t errors[] = {
      PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
      PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND,
      PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION,
  };
  auto tick_context = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(tick_context, protobuf_to_system_clock(younger->metadata().expire_timepoint()) -
                                                   std::chrono::system_clock::duration{1}));
  CASE_EXPECT_EQ(0, global_reject_calls);
  CASE_EXPECT_EQ(0, younger->resolve_times());
  for (size_t attempt = 0; attempt < 3; ++attempt) {
    decision_node_error = errors[attempt];
    handle->fire_resolve_custom_timer_for_unit_test(protobuf_to_system_clock(younger->resolve_timepoint()));
    CASE_EXPECT_TRUE(dt_test::wait_for(
        test, [&recorder, attempt]() { return recorder.count("on_resolve_task_finished") == attempt + 1; }));
    CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-younger").get());
    CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-extra").get());
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                   younger->metadata().status());
    CASE_EXPECT_EQ(attempt + 1, younger->resolve_times());
    CASE_EXPECT_EQ(replicated ? 2 : 0, younger->metadata().replicate_read_count());
    CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
    CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
    CASE_EXPECT_EQ(0, commit_acks + reject_acks);
    handle_type::snapshot_type snapshot;
    handle->dump(snapshot);
    CASE_EXPECT_EQ(1, snapshot.wounded_transaction_uuid_size());
    if (snapshot.wounded_transaction_uuid_size() == 1) {
      CASE_EXPECT_EQ(std::string("wound-younger"), snapshot.wounded_transaction_uuid(0));
    }
    handle->load(snapshot);
    younger = handle->get_locker("res-younger");
  }
  decision_node_error = 0;
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle, &commit_acks, &reject_acks]() {
    return commit_acks + reject_acks > 0 && handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_EQ(4 * nodes.size(), global_reject_calls);
  CASE_EXPECT_EQ(committed ? nodes.size() : 0, commit_acks);
  CASE_EXPECT_EQ(committed ? 0 : nodes.size(), reject_acks);
  CASE_EXPECT_EQ(committed ? 1 : 0, recorder.count("do_event"));
  CASE_EXPECT_EQ(committed ? 1 : 0, recorder.count("on_commited"));
  CASE_EXPECT_EQ(committed ? 0 : 1, recorder.count("on_rejected"));
  CASE_EXPECT_EQ(1, recorder.count("on_finished"));
  CASE_EXPECT_EQ(committed ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED
                           : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                 younger->metadata().status());
  CASE_EXPECT_FALSE(!!handle->get_locker("res-younger"));
  CASE_EXPECT_FALSE(!!handle->get_locker("res-extra"));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
  auto retry_task =
      test.run_task("wound_retry_lock", std::chrono::seconds{4}, [handle](rpc::context&) -> rpc::result_code_type {
        auto owner = handle->get_locker("res-older");
        google::protobuf::RepeatedPtrField<std::string> resources;
        resources.Add()->assign("res-younger");
        resources.Add()->assign("res-free");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(owner, resources)));
        CASE_EXPECT_EQ(owner.get(), handle->get_locker("res-younger").get());
        CASE_EXPECT_EQ(owner.get(), handle->get_locker("res-older").get());
        CASE_EXPECT_EQ(owner.get(), handle->get_locker("res-free").get());
        RPC_RETURN_CODE(0);
      });
  auto retry_result = test.wait(retry_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(retry_result.task_exited);
  CASE_EXPECT_EQ(0, retry_result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

}  // namespace

CASE_TEST(component_distributed_transaction_participator, resolve_releases_processed_storage_and_completed_owner) {
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
  bool second_entered = false;
  bool release_second = false;
  int writable_calls = 0;
  task_type_trait::task_type observed_task;
  vtable->check_writable = [&second_entered, &release_second, &writable_calls, &observed_task](
                               rpc::context& ctx, handle_type&, bool& writable) -> rpc::result_code_type {
    if (++writable_calls == 3) {
      observed_task = task_manager::me()->get_task(ctx.get_task_context().task_id);
      second_entered = true;
      while (!release_second) {
        auto result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
      }
    }
    writable = true;
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  atfw::util::memory::weak_rc_ptr<handle_type> owner_watcher = handle;
  atfw::util::memory::weak_rc_ptr<handle_type::storage_type> first_watcher;
  auto expiry = atfw::util::time::time_utility::now() + std::chrono::seconds{60};
  auto prepare_task = test.run_task(
      "prepare_release_batch", std::chrono::seconds{4},
      [&handle, &first_watcher, expiry](rpc::context& ctx) -> rpc::result_code_type {
        for (const auto* uuid : {"release-a", "release-b"}) {
          auto request = make_prepare_request(uuid, 2, std::chrono::seconds{60}, {uuid});
          *request.mutable_storage()->mutable_metadata()->mutable_expire_timepoint() =
              protobuf_from_system_clock(expiry);
          SSParticipatorTransactionPrepareRsp response;
          storage_ptr_type output;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
          if (std::string(uuid) == "release-a") {
            first_watcher = output;
          }
        }
        RPC_RETURN_CODE(0);
      });
  auto prepare_result = test.wait(prepare_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(prepare_result.task_exited);
  CASE_EXPECT_EQ(0, prepare_result.result_code);
  handle->fire_resolve_custom_timer_for_unit_test(expiry);
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&second_entered]() { return second_entered; }));
  CASE_EXPECT_FALSE(!!handle->get_locker("release-a"));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
  CASE_EXPECT_TRUE(first_watcher.expired());
  handle.reset();
  CASE_EXPECT_FALSE(owner_watcher.expired());  // The active callback still needs its owner.
  release_second = true;
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  CASE_EXPECT_TRUE(task_type_trait::is_exiting(observed_task));
  CASE_EXPECT_TRUE(owner_watcher.expired());  // Retaining a completed task must not retain the SDK.
  CASE_EXPECT_TRUE(first_watcher.expired());
  task_type_trait::reset_task(observed_task);
  CASE_EXPECT_TRUE(dt_test::expect_event_list(
      recorder.events, {"check_prepare", "on_start_running", "check_prepare", "on_start_running", "on_finish_running",
                        "on_finish_running", "on_resolve_task_finished"}));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, acknowledge_releases_completed_storage_before_next_response) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  handle_type::snapshot_type snapshot;
  auto expiry = atfw::util::time::time_utility::now() + std::chrono::seconds{60};
  for (const auto* uuid : {"ack-release-a", "ack-release-b"}) {
    auto request = make_prepare_request(uuid);
    auto* storage = snapshot.add_finished_transaction();
    *storage = request.storage();
    storage->mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);
    storage->set_finished_callback_completed(true);
    *storage->mutable_resolve_timepoint() = protobuf_from_system_clock(expiry);
  }
  handle->load(snapshot);
  atfw::util::memory::weak_rc_ptr<handle_type::storage_type> first_watcher =
      handle->get_finished_transactions().at("ack-release-a");
  bool second_entered = false;
  bool release_second = false;
  int ack_calls = 0;
  auto ack_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit_participator(),
      atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name(),
      [&second_entered, &release_second, &ack_calls](const atfw::testing::ss_request_view& view,
                                                     google::protobuf::Message& response) -> rpc::result_code_type {
        const auto& request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq&>(view.body);
        ++ack_calls;
        if (request.metadata().transaction_uuid() == "ack-release-b") {
          second_entered = true;
          if (!CASE_EXPECT_NE(nullptr, view.context)) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          while (!release_second) {
            auto result = RPC_AWAIT_CODE_RESULT(rpc::wait(*view.context, std::chrono::milliseconds{1}));
            if (result < 0) {
              RPC_RETURN_CODE(result);
            }
          }
        }
        auto& output = static_cast<atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp&>(response);
        *output.mutable_metadata() = request.metadata();
        output.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ack_rule);
  handle->fire_resolve_custom_timer_for_unit_test(expiry);
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&second_entered]() { return second_entered; }));
  CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
  CASE_EXPECT_TRUE(first_watcher.expired());
  release_second = true;
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(2, ack_calls);
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"on_resolve_task_finished"}));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, terminal_notification_finishes_before_ack) {
  for (bool commit : {false, true}) {
    for (int32_t notification_result : {0, static_cast<int32_t>(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT)}) {
      atfw::testing::runtime test;
      atfw::testing::runtime_options options;
      options.features = {atfw::testing::feature::ss};
      CASE_EXPECT_EQ(0, test.start(options));
      if (!test.is_running()) {
        return;
      }
      CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
      participator_event_recorder recorder;
      auto vtable = recorder.make_vtable();
      int ack_calls = 0;
      auto ack_rule = register_participator_ack_mock(test, commit ? "commit" : "reject", &ack_calls);
      CASE_EXPECT_TRUE(!!ack_rule);
      const auto pending_status = commit ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING
                                         : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING;
      auto observe = [](const decltype(vtable->do_event)& original, EnDistibutedTransactionStatus expected,
                        bool running) {
        return [original, expected, running](rpc::context& ctx, handle_type& owner,
                                             const handle_type::storage_type& storage) -> rpc::result_code_type {
          CASE_EXPECT_EQ(expected, storage.metadata().status());
          CASE_EXPECT_EQ(running ? 1 : 0, owner.get_running_transactions().size());
          CASE_EXPECT_EQ(running ? 0 : 1, owner.get_finished_transactions().size());
          CASE_EXPECT_EQ(running ? &storage : nullptr, owner.get_locker("notification-lock").get());
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(original(ctx, owner, storage)));
        };
      };
      vtable->on_start_running = observe(
          vtable->on_start_running, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED, true);
      vtable->do_event =
          observe(vtable->do_event, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING, true);
      vtable->on_finish_running = observe(vtable->on_finish_running, pending_status, false);
      vtable->on_finished = observe(vtable->on_finished, pending_status, false);
      bool notification_entered = false;
      bool release_notification = false;
      auto notification = [&recorder, &notification_entered, &release_notification, &ack_calls, pending_status,
                           notification_result](rpc::context& ctx, handle_type& owner,
                                                const handle_type::storage_type& storage) -> rpc::result_code_type {
        recorder.events.emplace_back("notification_enter");
        CASE_EXPECT_EQ(pending_status, storage.metadata().status());
        CASE_EXPECT_FALSE(owner.has_resolve_custom_timer_for_unit_test());
        notification_entered = true;
        while (!release_notification) {
          auto res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
          if (res < 0) {
            RPC_RETURN_CODE(res);
          }
        }
        CASE_EXPECT_EQ(0, ack_calls);
        CASE_EXPECT_EQ(pending_status, storage.metadata().status());
        CASE_EXPECT_EQ(1, owner.get_finished_transactions().size());
        recorder.events.emplace_back("notification_exit");
        RPC_RETURN_CODE(notification_result);
      };
      if (commit) {
        vtable->on_commited = notification;
      } else {
        vtable->on_rejected = notification;
      }
      auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
      atfw::util::memory::weak_rc_ptr<handle_type> handle_watcher = handle;
      atfw::util::memory::weak_rc_ptr<handle_type::storage_type> storage_watcher;
      auto task = test.run_task(
          "notification_before_ack", std::chrono::seconds{4},
          [&handle, &storage_watcher, commit](rpc::context& ctx) -> rpc::result_code_type {
            auto request =
                make_prepare_request("notification-before-ack", 2, std::chrono::seconds{60}, {"notification-lock"});
            SSParticipatorTransactionPrepareRsp response;
            storage_ptr_type output;
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
            storage_watcher = output;
            if (commit) {
              SSParticipatorTransactionCommitReq terminal_request;
              SSParticipatorTransactionCommitRsp terminal_response;
              terminal_request.set_transaction_uuid("notification-before-ack");
              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, terminal_request, terminal_response)));
            }
            SSParticipatorTransactionRejectReq terminal_request;
            SSParticipatorTransactionRejectRsp terminal_response;
            terminal_request.set_transaction_uuid("notification-before-ack");
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->reject(ctx, terminal_request, terminal_response)));
          });
      CASE_EXPECT_TRUE(dt_test::wait_for(test, [&notification_entered]() { return notification_entered; }));
      auto ctx = atfw::testing::make_context();
      CASE_EXPECT_EQ(0, handle->tick(ctx, atfw::util::time::time_utility::now() + std::chrono::hours{1}));
      CASE_EXPECT_EQ(0, recorder.check_writable_calls);
      release_notification = true;
      auto result = test.wait(task, std::chrono::seconds{8});
      CASE_EXPECT_TRUE(result.task_exited);
      CASE_EXPECT_EQ(0, result.result_code);
      CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle]() { return handle->get_finished_transactions().empty(); }));
      std::vector<std::string> expected = {"check_prepare", "on_start_running"};
      if (commit) {
        expected.emplace_back("do_event");
      }
      expected.insert(expected.end(), {
                                          "on_finish_running",
                                          "on_finished",
                                          "notification_enter",
                                          "notification_exit",
                                          "on_resolve_task_finished",
                                      });
      CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, expected));
      CASE_EXPECT_EQ(1, ack_calls);
      CASE_EXPECT_FALSE(!!handle->get_locker("notification-lock"));
      CASE_EXPECT_TRUE(storage_watcher.expired());
      CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
      handle.reset();
      CASE_EXPECT_TRUE(handle_watcher.expired());
      CASE_EXPECT_EQ(0, test.stop());
    }
  }
}

CASE_TEST(component_distributed_transaction_participator, terminal_notification_reload_preserves_recovery_timer) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  handle_type::snapshot_type saved;
  vtable->on_commited = [&saved, &recorder](rpc::context&, handle_type& owner,
                                            const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                   storage.metadata().status());
    recorder.events.emplace_back("on_commited");
    owner.load(saved);
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  atfw::util::memory::weak_rc_ptr<handle_type::storage_type> old_watcher;
  auto task = test.run_task(
      "notification_reload", std::chrono::seconds{4},
      [&handle, &saved, &old_watcher](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("notification-reload", 2, std::chrono::seconds{60}, {"reload-lock"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type storage;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, storage)));
        old_watcher = storage;
        handle->dump(saved);
        SSParticipatorTransactionCommitReq commit_request;
        SSParticipatorTransactionCommitRsp commit_response;
        commit_request.set_transaction_uuid("notification-reload");
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_NE(storage.get(), handle->get_locker("reload-lock").get());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_TRUE(old_watcher.expired());
  handle_type::snapshot_type actual;
  handle->dump(actual);
  CASE_EXPECT_EQ(saved.SerializeAsString(), actual.SerializeAsString());
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  if (CASE_EXPECT_EQ(1, saved.running_transaction_size())) {
    CASE_EXPECT_EQ(protobuf_to_system_clock(saved.running_transaction(0).resolve_timepoint()),
                   handle->get_resolve_custom_timer_timepoint_for_unit_test());
  }
  CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, {"check_prepare", "on_start_running", "do_event",
                                                                "on_finish_running", "on_finished", "on_commited"}));
  auto timer_watcher = handle->get_resolve_custom_timer_watcher_for_unit_test();
  handle.reset();
  CASE_EXPECT_TRUE(timer_watcher.expired());
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, callback_task_timeout_cleans_each_completion_stage) {
  for (bool force_commit : {false, true}) {
    for (int stage : {0, 1, 2}) {
      atfw::testing::runtime test;
      atfw::testing::runtime_options options;
      options.features = {atfw::testing::feature::ss};
      CASE_EXPECT_EQ(0, test.start(options));
      if (!test.is_running()) {
        return;
      }
      CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
      participator_event_recorder recorder;
      auto vtable = recorder.make_vtable();
      int timeout_result = 0;
      auto* callback = &vtable->do_event;
      if (stage == 1) {
        callback = &vtable->on_finished;
      } else if (stage == 2) {
        callback = &vtable->on_commited;
      }
      auto original = *callback;
      *callback = [original, &timeout_result, force_commit, stage](
                      rpc::context& ctx, handle_type& owner,
                      const handle_type::storage_type& storage) -> rpc::result_code_type {
        CASE_EXPECT_EQ(force_commit && stage == 2
                           ? EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED
                           : EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                       storage.metadata().status());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(original(ctx, owner, storage)));
        timeout_result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::seconds{1}));
        RPC_RETURN_CODE(timeout_result);
      };
      auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
      atfw::util::memory::weak_rc_ptr<handle_type> owner_watcher = handle;
      atfw::util::memory::weak_rc_ptr<handle_type::storage_type> storage_watcher;
      int ack_calls = 0;
      auto ack_rule = register_participator_ack_mock(test, "commit", &ack_calls);
      CASE_EXPECT_TRUE(!!ack_rule);
      auto task = test.run_task(
          "completion_stage_timeout", std::chrono::milliseconds{20},
          [&handle, &storage_watcher, force_commit](rpc::context& ctx) -> rpc::result_code_type {
            auto request = make_prepare_request("stage-timeout", 1, std::chrono::seconds{60}, {"stage-lock"});
            request.mutable_storage()->mutable_configure()->set_force_commit(force_commit);
            SSParticipatorTransactionPrepareRsp response;
            storage_ptr_type output;
            auto prepare_result = RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output));
            if (force_commit) {
              RPC_RETURN_CODE(prepare_result);
            }
            CASE_EXPECT_EQ(0, prepare_result);
            storage_watcher = output;
            SSParticipatorTransactionCommitReq commit_request;
            SSParticipatorTransactionCommitRsp commit_response;
            commit_request.set_transaction_uuid("stage-timeout");
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
          });
      auto result = test.wait(task, std::chrono::seconds{8});
      CASE_EXPECT_TRUE(result.task_exited);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, result.result_code);
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, timeout_result);
      const bool acknowledge = !force_commit && stage != 1;
      if (acknowledge) {
        CASE_EXPECT_TRUE(
            drive_handle(test, handle, [&handle]() { return handle->get_finished_transactions().empty(); }));
      }
      std::vector<std::string> expected = {"check_prepare", "on_start_running", "do_event", "on_finish_running"};
      if (!force_commit || stage != 0) {
        expected.emplace_back("on_finished");
      }
      if ((force_commit && stage != 0) || (!force_commit && stage != 1)) {
        expected.emplace_back("on_commited");
      }
      if (acknowledge) {
        expected.emplace_back("on_resolve_task_finished");
      }
      CASE_EXPECT_TRUE(dt_test::expect_event_list(recorder.events, expected));
      CASE_EXPECT_EQ(acknowledge ? 1 : 0, ack_calls);
      CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
      CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
      CASE_EXPECT_FALSE(!!handle->get_locker("stage-lock"));
      CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
      CASE_EXPECT_TRUE(storage_watcher.expired());
      handle.reset();
      CASE_EXPECT_TRUE(owner_watcher.expired());
      CASE_EXPECT_EQ(0, test.stop());
    }
  }
}

CASE_TEST(component_distributed_transaction_participator, force_commit_events_preserve_state_and_release_owner) {
  for (bool fail_action : {false, true}) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      return;
    }
    std::vector<std::string> events;
    auto record = [&events](const char* event, EnDistibutedTransactionStatus expected, int32_t result) {
      return [&events, event, expected, result](rpc::context&, handle_type& owner,
                                                const handle_type::storage_type& storage) -> rpc::result_code_type {
        events.emplace_back(event);
        CASE_EXPECT_EQ(expected, storage.metadata().status());
        CASE_EXPECT_TRUE(owner.get_running_transactions().empty());
        CASE_EXPECT_TRUE(owner.get_finished_transactions().empty());
        CASE_EXPECT_FALSE(owner.has_resolve_custom_timer_for_unit_test());
        CASE_EXPECT_FALSE(!!owner.get_locker("force-event-lock"));
        RPC_RETURN_CODE(result);
      };
    };
    auto vtable = atfw::component::memory::stl::make_strong_rc<handle_type::vtable_type>();
    vtable->check_prepare =
        [&events](rpc::context&, handle_type&, handle_type::storage_type& storage,
                  atfw::distributed_system::transaction_participator_failure_reason&) -> rpc::result_code_type {
      events.emplace_back("check_prepare");
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED,
                     storage.metadata().status());
      RPC_RETURN_CODE(0);
    };
    const auto preparing = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED;
    const auto committing = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING;
    const auto committed = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED;
    const auto rejected = EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED;
    vtable->on_start_running = record("on_start_running", preparing, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    vtable->do_event = record("do_event", committing, fail_action ? PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT : 0);
    vtable->on_finish_running = record("on_finish_running", committing, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    vtable->on_finished = record("on_finished", committing, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    vtable->on_commited = record("on_commited", committed, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    vtable->undo_event = record("undo_event", rejected, 0);
    vtable->on_rejected = record("on_rejected", rejected, 0);
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
    atfw::util::memory::weak_rc_ptr<handle_type> owner_watcher = handle;
    auto task = test.run_task(
        "force_event_sequence", std::chrono::seconds{4},
        [&handle, fail_action](rpc::context& ctx) -> rpc::result_code_type {
          auto request = make_prepare_request("force-events", 2, std::chrono::seconds{60}, {"force-event-lock"});
          request.mutable_storage()->mutable_metadata()->clear_status();
          request.mutable_storage()->mutable_configure()->set_force_commit(true);
          SSParticipatorTransactionRejectReq undo;
          undo.set_transaction_uuid("force-events");
          *undo.mutable_storage() = request.storage();
          undo.mutable_storage()->mutable_metadata()->set_status(
              EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
          SSParticipatorTransactionPrepareRsp response;
          storage_ptr_type output;
          CASE_EXPECT_EQ(fail_action ? PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT : 0,
                         RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
          CASE_EXPECT_FALSE(!!output);
          if (fail_action) {
            SSParticipatorTransactionRejectRsp undo_response;
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, undo, undo_response)));
          }
          RPC_RETURN_CODE(0);
        });
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    std::vector<std::string> expected = {"check_prepare", "on_start_running", "do_event", "on_finish_running"};
    if (fail_action) {
      expected.emplace_back("undo_event");
    } else {
      expected.insert(expected.end(), {"on_finished", "on_commited"});
    }
    CASE_EXPECT_TRUE(dt_test::expect_event_list(events, expected));
    handle.reset();
    CASE_EXPECT_TRUE(owner_watcher.expired());
    CASE_EXPECT_EQ(0, test.stop());
  }
}

CASE_TEST(component_distributed_transaction_participator, wound_follows_confirmed_commit) {
  run_wound_decision(false, true);
}

CASE_TEST(component_distributed_transaction_participator, wound_uses_configured_replica_quorum) {
  run_wound_decision(true, false);
}

CASE_TEST(component_distributed_transaction_participator, wound_follows_committed_replica) {
  run_wound_decision(true, true);
}

CASE_TEST(component_distributed_transaction_participator, wound_timeout_cleanup) {
  for (bool unwritable : {false, true}) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      return;
    }
    CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
    participator_event_recorder recorder;
    recorder.check_writable_result = !unwritable;
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
    storage_ptr_type older;
    storage_ptr_type younger;
    auto prepare_task =
        test.run_task("expiring_wound", std::chrono::seconds{4},
                      [handle, &older, &younger](rpc::context& ctx) -> rpc::result_code_type {
                        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(prepare_wound(ctx, handle, older, younger)));
                      });
    auto prepare_result = test.wait(prepare_task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(prepare_result.task_exited);
    CASE_EXPECT_EQ(0, prepare_result.result_code);
    if (!younger) {
      test.stop();
      return;
    }
    auto reject_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_reject(),
                                            PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    CASE_EXPECT_TRUE(!!reject_rule);
    auto ctx = atfw::testing::make_context();
    const auto expiry = protobuf_to_system_clock(younger->metadata().expire_timepoint());
    CASE_EXPECT_EQ(0, handle->tick(ctx, expiry - std::chrono::system_clock::duration{1}));
    CASE_EXPECT_EQ(0, recorder.check_writable_calls);
    CASE_EXPECT_EQ(0, younger->resolve_times());
    handle->fire_resolve_custom_timer_for_unit_test(expiry);
    CASE_EXPECT_TRUE(
        dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
    CASE_EXPECT_EQ(1, younger->resolve_times());
    CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-younger").get());
    CASE_EXPECT_EQ(younger.get(), handle->get_locker("res-extra").get());
    handle_type::snapshot_type snapshot;
    handle->dump(snapshot);
    handle->load(snapshot);
    younger = handle->get_locker("res-younger");
    older = handle->get_locker("res-older");
    CASE_EXPECT_TRUE(handle->get_running_transactions().at("wound-younger").wounded);
    CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return !handle->get_locker("res-younger"); }));
    CASE_EXPECT_EQ(2, younger->resolve_times());
    CASE_EXPECT_EQ(unwritable ? 2 : 3, recorder.count("on_resolve_task_finished"));
    CASE_EXPECT_EQ(unwritable ? 0 : 2, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
    CASE_EXPECT_FALSE(!!handle->get_locker("res-extra"));
    CASE_EXPECT_EQ(older.get(), handle->get_locker("res-older").get());
    CASE_EXPECT_EQ(unwritable ? 0 : 1, recorder.count("on_finish_running"));
    CASE_EXPECT_EQ(0, recorder.count("do_event"));
    CASE_EXPECT_EQ(0, recorder.count("on_finished"));
    CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject_participator()));
    CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
    CASE_EXPECT_EQ(0, test.stop());
  }
}

CASE_TEST(component_distributed_transaction_participator, wound_terminal_notification_before_expiry) {
  for (bool commit : {false, true}) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      return;
    }
    CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
    participator_event_recorder recorder;
    recorder.do_event_script = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
    auto vtable = recorder.make_vtable();
    auto original_do = vtable->do_event;
    vtable->do_event = [original_do](rpc::context& ctx, handle_type& owner,
                                     const handle_type::storage_type& storage) -> rpc::result_code_type {
      CASE_EXPECT_EQ(&storage, owner.get_locker("res-younger").get());
      CASE_EXPECT_EQ(&storage, owner.get_locker("res-extra").get());
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                     storage.metadata().status());
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(original_do(ctx, owner, storage)));
    };
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
    int ack_calls = 0;
    auto ack_rule = register_participator_ack_mock(test, commit ? "commit" : "reject", &ack_calls);
    auto global_reject_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_reject(),
                                                   PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    CASE_EXPECT_TRUE(!!ack_rule && !!global_reject_rule);
    auto task = test.run_task(
        "wound_notified", std::chrono::seconds{4}, [handle, commit](rpc::context& ctx) -> rpc::result_code_type {
          storage_ptr_type older;
          storage_ptr_type younger;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(prepare_wound(ctx, handle, older, younger)));
          handle_type::snapshot_type snapshot;
          handle->dump(snapshot);
          handle->load(snapshot);
          if (commit) {
            SSParticipatorTransactionCommitReq request;
            SSParticipatorTransactionCommitRsp response;
            request.set_transaction_uuid("wound-younger");
            CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                           RPC_AWAIT_CODE_RESULT(handle->commit(ctx, request, response)));
            CASE_EXPECT_EQ(1, handle->get_locker("res-younger")->resolve_times());
            handle->dump(snapshot);
            handle->load(snapshot);
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, request, response)));
          } else {
            SSParticipatorTransactionRejectReq request;
            SSParticipatorTransactionRejectRsp response;
            request.set_transaction_uuid("wound-younger");
            CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, request, response)));
            SSParticipatorTransactionCommitReq commit_request;
            SSParticipatorTransactionCommitRsp commit_response;
            commit_request.set_transaction_uuid("wound-younger");
            for (int attempt = 0; attempt < 2; ++attempt) {
              CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
              CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                             handle->get_finished_transactions().at("wound-younger")->metadata().status());
              handle->dump(snapshot);
              CASE_EXPECT_EQ(0, snapshot.wounded_transaction_uuid_size());
              handle->load(snapshot);
            }
          }
          CASE_EXPECT_LT(atfw::util::time::time_utility::now(),
                         protobuf_to_system_clock(younger->metadata().expire_timepoint()));
          CASE_EXPECT_FALSE(!!handle->get_locker("res-younger"));
          CASE_EXPECT_FALSE(!!handle->get_locker("res-extra"));
          CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
          RPC_RETURN_CODE(0);
        });
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return handle->get_finished_transactions().empty(); }));
    CASE_EXPECT_EQ(commit ? 2 : 0, recorder.count("do_event"));
    CASE_EXPECT_EQ(1, recorder.count(commit ? "on_commited" : "on_rejected"));
    CASE_EXPECT_EQ(1, ack_calls);
    CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
    CASE_EXPECT_EQ(0, test.stop());
  }
}

CASE_TEST(component_distributed_transaction_participator, stale_wound_response_preserves_reloaded_locks) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  storage_ptr_type older;
  storage_ptr_type younger;
  auto prepare_task =
      test.run_task("stale_wound_prepare", std::chrono::seconds{4},
                    [handle, &older, &younger](rpc::context& ctx) -> rpc::result_code_type {
                      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(prepare_wound(ctx, handle, older, younger)));
                    });
  auto prepare_result = test.wait(prepare_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(prepare_result.task_exited);
  CASE_EXPECT_EQ(0, prepare_result.result_code);
  int reject_calls = 0;
  auto reject_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject(),
      atfw::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionRejectRsp::descriptor()->full_name(),
      [handle, &reject_calls](const atfw::testing::ss_request_view& request,
                              google::protobuf::Message& response) -> rpc::result_code_type {
        if (++reject_calls == 1) {
          handle_type::snapshot_type snapshot;
          handle->dump(snapshot);
          handle->load(snapshot);
        }
        const auto& in = static_cast<const atfw::distributed_system::SSDistributeTransactionRejectReq&>(request.body);
        auto& out = static_cast<atfw::distributed_system::SSDistributeTransactionRejectRsp&>(response);
        *out.mutable_metadata() = in.metadata();
        out.mutable_metadata()->set_status(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
        RPC_RETURN_CODE(0);
      });
  int reject_acks = 0;
  auto ack_rule = register_participator_ack_mock(test, "reject", &reject_acks);
  CASE_EXPECT_TRUE(!!reject_rule && !!ack_rule);
  if (younger) {
    handle->fire_resolve_custom_timer_for_unit_test(protobuf_to_system_clock(younger->resolve_timepoint()));
    CASE_EXPECT_TRUE(
        dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
    auto restored = handle->get_locker("res-younger");
    CASE_EXPECT_TRUE(!!restored);
    CASE_EXPECT_NE(younger.get(), restored.get());
    CASE_EXPECT_EQ(restored.get(), handle->get_locker("res-extra").get());
    if (restored) {
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                     restored->metadata().status());
    }
    CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
    CASE_EXPECT_EQ(0, reject_acks);
    CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
    CASE_EXPECT_TRUE(drive_handle(test, handle, [&reject_acks, handle]() {
      return reject_acks == 1 && handle->get_finished_transactions().empty();
    }));
    CASE_EXPECT_EQ(2, reject_calls);
    CASE_EXPECT_FALSE(!!handle->get_locker("res-younger"));
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, snapshot_ack_preserves_pending_and_terminal_states) {
  for (auto status : {
           EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
           EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
           EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
           EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
       }) {
    for (bool finished : {false, true}) {
      run_snapshot_ack_lifecycle(status, finished);
    }
  }
}

CASE_TEST(component_distributed_transaction_participator, normal_state_lifecycle_with_optional_do_event) {
  for (bool commit : {true, false}) {
    for (bool with_do_event : {true, false}) {
      run_normal_state_lifecycle(commit, with_do_event, false);
    }
  }
}

CASE_TEST(component_distributed_transaction_participator, on_finished_exhaustion_preserves_pending_state) {
  run_normal_state_lifecycle(true, true, true);
  run_normal_state_lifecycle(false, false, true);
}

CASE_TEST(component_distributed_transaction_participator, force_commit_state_lifecycle_keeps_callback_error_semantics) {
  for (bool with_do_event : {true, false}) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      return;
    }
    participator_event_recorder recorder;
    auto vtable = recorder.make_vtable();
    int finish_calls = 0;
    vtable->on_start_running = [](rpc::context&, handle_type&,
                                  const handle_type::storage_type& storage) -> rpc::result_code_type {
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                     storage.metadata().status());
      RPC_RETURN_CODE(0);
    };
    vtable->on_finish_running = [](rpc::context&, handle_type&,
                                   const handle_type::storage_type& storage) -> rpc::result_code_type {
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                     storage.metadata().status());
      RPC_RETURN_CODE(0);
    };
    if (with_do_event) {
      vtable->do_event = [&recorder](rpc::context&, handle_type&,
                                     const handle_type::storage_type& storage) -> rpc::result_code_type {
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                       storage.metadata().status());
        recorder.events.emplace_back("do_event");
        RPC_RETURN_CODE(0);
      };
    } else {
      vtable->do_event = nullptr;
    }
    vtable->on_finished = [&finish_calls](rpc::context&, handle_type&,
                                          const handle_type::storage_type& storage) -> rpc::result_code_type {
      ++finish_calls;
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                     storage.metadata().status());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    };
    vtable->on_commited = [&recorder](rpc::context&, handle_type&,
                                      const handle_type::storage_type& storage) -> rpc::result_code_type {
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                     storage.metadata().status());
      recorder.events.emplace_back("on_commited");
      RPC_RETURN_CODE(0);
    };
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
    auto task = test.run_task(
        "force_state_lifecycle", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
          auto request = make_prepare_request("force-state-lifecycle");
          request.mutable_storage()->mutable_metadata()->clear_status();
          request.mutable_storage()->mutable_configure()->set_force_commit(true);
          SSParticipatorTransactionPrepareRsp response;
          storage_ptr_type output;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
          CASE_EXPECT_FALSE(!!output);
          RPC_RETURN_CODE(0);
        });
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_EQ(with_do_event ? 1 : 0, recorder.count("do_event"));
    CASE_EXPECT_EQ(1, finish_calls);
    CASE_EXPECT_EQ(1, recorder.count("on_commited"));
    CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
    CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
    CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
    CASE_EXPECT_EQ(0, test.stop());
  }
}

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

        // Local callbacks finish before the coordinator ACK confirms COMMITED.
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

CASE_TEST(component_distributed_transaction_participator, prepare_conflict_keeps_original_locks) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task = test.run_task(
      "prepare_conflict", std::chrono::seconds{4}, [handle, &recorder](rpc::context& ctx) -> rpc::result_code_type {
        auto older_request = make_prepare_request("older", 2, std::chrono::minutes{10}, {"free", "held", "held"});
        auto younger_request = make_prepare_request("younger", 2, std::chrono::minutes{10}, {"held", "extra"});
        *older_request.mutable_storage()->mutable_metadata()->mutable_prepare_timepoint() =
            protobuf_from_system_clock(atfw::util::time::time_utility::now() - std::chrono::seconds{1});
        younger_request.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval()->set_seconds(60);
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type younger;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(younger_request), response, younger)));
        if (!younger) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
        }
        storage_ptr_type older;
        SSParticipatorTransactionPrepareReq retry_request = older_request;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(older_request), response, older)));
        CASE_EXPECT_TRUE(response.reason().allow_retry());
        CASE_EXPECT_FALSE(!!older);
        CASE_EXPECT_FALSE(!!handle->get_locker("free"));
        CASE_EXPECT_EQ(younger.get(), handle->get_locker("held").get());
        CASE_EXPECT_EQ(younger.get(), handle->get_locker("extra").get());
        CASE_EXPECT_EQ(2, younger->lock_resource_size());
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        CASE_EXPECT_EQ(1, recorder.count("on_start_running"));
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                       younger->metadata().status());
        const auto resolve_timepoint = protobuf_to_system_clock(younger->resolve_timepoint());
        response.Clear();
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(retry_request), response, older)));
        CASE_EXPECT_TRUE(response.reason().allow_retry());
        CASE_EXPECT_EQ(resolve_timepoint, protobuf_to_system_clock(younger->resolve_timepoint()));
        CASE_EXPECT_EQ(younger.get(), handle->get_locker("held").get());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, blocked_prepare_does_not_wound_other_holders) {
  for (bool blocker_committing : {false, true}) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      return;
    }
    participator_event_recorder recorder;
    recorder.do_event_script = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
    auto task = test.run_task(
        "blocked_prepare", std::chrono::seconds{4},
        [handle, blocker_committing](rpc::context& ctx) -> rpc::result_code_type {
          auto blocker_request = make_prepare_request("blocker", 2, std::chrono::minutes{10}, {"blocked-resource"});
          auto victim_request = make_prepare_request("victim", 2, std::chrono::minutes{10}, {"victim-resource"});
          const auto contender_time =
              protobuf_to_system_clock(victim_request.storage().metadata().prepare_timepoint()) -
              std::chrono::seconds{1};
          if (!blocker_committing) {
            *blocker_request.mutable_storage()->mutable_metadata()->mutable_prepare_timepoint() =
                protobuf_from_system_clock(contender_time - std::chrono::seconds{1});
          }
          SSParticipatorTransactionPrepareRsp response;
          storage_ptr_type blocker;
          storage_ptr_type victim;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(blocker_request), response, blocker)));
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(victim_request), response, victim)));
          if (!blocker || !victim) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          if (blocker_committing) {
            SSParticipatorTransactionCommitReq request;
            SSParticipatorTransactionCommitRsp commit_response;
            request.set_transaction_uuid("blocker");
            CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                           RPC_AWAIT_CODE_RESULT(handle->commit(ctx, request, commit_response)));
          }
          const auto blocker_before = blocker->SerializeAsString();
          const auto victim_before = victim->SerializeAsString();
          for (bool reversed : {false, true}) {
            const auto resources = reversed ? std::vector<std::string>{"free", "blocked-resource", "victim-resource"}
                                            : std::vector<std::string>{"free", "victim-resource", "blocked-resource"};
            auto request = make_prepare_request("contender", 2, std::chrono::minutes{10}, resources);
            *request.mutable_storage()->mutable_metadata()->mutable_prepare_timepoint() =
                protobuf_from_system_clock(contender_time);
            storage_ptr_type output;
            response.Clear();
            CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                           RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
            CASE_EXPECT_TRUE(response.reason().allow_retry());
            CASE_EXPECT_FALSE(!!output);
            CASE_EXPECT_FALSE(!!handle->get_locker("free"));
            CASE_EXPECT_EQ(blocker.get(), handle->get_locker("blocked-resource").get());
            CASE_EXPECT_EQ(victim.get(), handle->get_locker("victim-resource").get());
            CASE_EXPECT_EQ(blocker_before, blocker->SerializeAsString());
            CASE_EXPECT_EQ(victim_before, victim->SerializeAsString());
            handle_type::snapshot_type snapshot;
            handle->dump(snapshot);
            CASE_EXPECT_EQ(2, snapshot.running_transaction_size());
            CASE_EXPECT_EQ(0, snapshot.wounded_transaction_uuid_size());
          }
          RPC_RETURN_CODE(0);
        });
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
    CASE_EXPECT_EQ(0, test.stop());
  }
}

CASE_TEST(component_distributed_transaction_participator, lock_wound_and_preemption_dt022) {
  run_wound_decision(false, false);
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

        // Reload pending/terminal work: even an older contender cannot change its direction or steal locks.
        auto contender = atfw::component::memory::stl::make_strong_rc<handle_type::storage_type>();
        *contender->mutable_metadata() = metadata;
        contender->mutable_metadata()->set_transaction_uuid("part-uuid-aaa");
        contender->add_lock_resource("res-1");
        for (auto status : {
                 EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                 EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                 EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                 EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
             }) {
          handle_type::snapshot_type snapshot;
          *snapshot.add_running_transaction() = *output;
          snapshot.mutable_running_transaction(0)->mutable_metadata()->set_status(status);
          handle->load(snapshot);
          auto restored = handle->get_locker("res-1");
          const bool was_wounded = handle->get_running_transactions().at("part-uuid-holder").wounded;
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                         handle->check_lock(contender->metadata(), resources, preemption));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                         RPC_AWAIT_CODE_RESULT(handle->lock(contender, contender->lock_resource())));
          CASE_EXPECT_EQ(restored.get(), handle->get_locker("res-1").get());
          CASE_EXPECT_EQ(status, restored->metadata().status());
          CASE_EXPECT_EQ(was_wounded, handle->get_running_transactions().at("part-uuid-holder").wounded);
        }
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
  auto commit_ack_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit_participator(),
      atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name(),
      [&commit_ack_calls](const atfw::testing::ss_request_view& request,
                          google::protobuf::Message& response) -> rpc::result_code_type {
        ++commit_ack_calls;
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq&>(request.body);
        auto& typed_response =
            static_cast<atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        protobuf_copy_message(*typed_response.mutable_metadata()->mutable_finish_timepoint(),
                              typed_request.metadata().prepare_timepoint());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!query_rule && !!commit_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto do_event = vtable->do_event;
  vtable->do_event = [do_event](rpc::context& ctx, handle_type& owner,
                                const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage.metadata().status());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(do_event(ctx, owner, storage)));
  };
  vtable->on_finished = [](rpc::context&, handle_type&,
                           const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   storage.metadata().status());
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type output;

  auto task = test.run_task(
      "resolve_prepare", std::chrono::seconds{4}, [&handle, &output](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("part-uuid-resolve-commit", 2, std::chrono::milliseconds{20});
        SSParticipatorTransactionPrepareRsp response;
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
  CASE_EXPECT_TRUE(!!output);
  if (output) {
    CASE_EXPECT_EQ(output->metadata().prepare_timepoint().seconds(), output->metadata().finish_timepoint().seconds());
    CASE_EXPECT_EQ(output->metadata().prepare_timepoint().nanos(), output->metadata().finish_timepoint().nanos());
  }
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
  auto reject_ack_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject_participator(),
      atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp::descriptor()->full_name(),
      [&reject_ack_calls](const atfw::testing::ss_request_view& request,
                          google::protobuf::Message& response) -> rpc::result_code_type {
        ++reject_ack_calls;
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq&>(request.body);
        auto& typed_response =
            static_cast<atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        // A participator ack updates participator_status, not the coordinator's global decision.
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!query_rule && !!reject_ack_rule);

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type output;

  auto task = test.run_task(
      "budget_exhaustion", std::chrono::seconds{4}, [&handle, &output](rpc::context& ctx) -> rpc::result_code_type {
        // resolve_times == resolve_max_times already: the next resolve entry goes straight to the
        // local reject, with zero coordinator queries.
        auto request = make_prepare_request("part-uuid-budget", 2, std::chrono::milliseconds{20}, {}, 2);
        SSParticipatorTransactionPrepareRsp response;
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
  CASE_EXPECT_TRUE(!!output);
  if (output) {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   output->metadata().status());
  }
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

CASE_TEST(component_distributed_transaction_participator, finished_ack_merges_only_confirmed_replica_state) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001, 0x1B0002}));
  int reject_ack_calls = 0;
  auto ack_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_reject_participator(),
      atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp::descriptor()->full_name(),
      [&reject_ack_calls](const atfw::testing::ss_request_view& request,
                          google::protobuf::Message& response) -> rpc::result_code_type {
        ++reject_ack_calls;
        const auto& typed_request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq&>(request.body);
        CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                       typed_request.metadata().status());
        CASE_EXPECT_EQ(0, typed_request.metadata().finish_timepoint().seconds());
        if (reject_ack_calls == 2) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
        }
        auto& typed_response =
            static_cast<atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp&>(response);
        protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
        typed_response.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        protobuf_copy_message(*typed_response.mutable_metadata()->mutable_finish_timepoint(),
                              typed_request.metadata().prepare_timepoint());
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ack_rule);
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  storage_ptr_type output;
  auto task = test.run_task(
      "prepare_ack_replicas", std::chrono::seconds{4}, [handle, &output](rpc::context& ctx) -> rpc::result_code_type {
        // Query budget exhaustion can precede learning the coordinator's committed decision.
        auto request = make_prepare_request("ack-replica-decision", 3, std::chrono::milliseconds{20}, {"res-ack"}, 3);
        auto* metadata = request.mutable_storage()->mutable_metadata();
        metadata->set_replicate_read_count(2);
        metadata->add_replicate_node_server_id(0x1B0001);
        metadata->add_replicate_node_server_id(0x1B0002);
        SSParticipatorTransactionPrepareRsp response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() {
    return handle->get_running_transactions().empty() && handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_EQ(4, reject_ack_calls);
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit_participator()));
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_query()));
  CASE_EXPECT_TRUE(!!output);
  if (output) {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                   output->metadata().status());
    CASE_EXPECT_EQ(output->metadata().prepare_timepoint().seconds(), output->metadata().finish_timepoint().seconds());
    CASE_EXPECT_EQ(output->metadata().prepare_timepoint().nanos(), output->metadata().finish_timepoint().nanos());
  }
  CASE_EXPECT_EQ(1, recorder.count("on_rejected"));
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_FALSE(!!handle->get_locker("res-ack"));
  CASE_EXPECT_EQ(0, test.stop());
}

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

        auto request =
            make_prepare_request("part-uuid-readonly", 2, std::chrono::milliseconds{60000}, {"res-readonly"});
        *request.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval() =
            protobuf_from_chrono_duration(std::chrono::seconds{60});
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
  CASE_EXPECT_EQ(1, running_iter->second.storage->resolve_times());
  CASE_EXPECT_TRUE(!!handle->get_locker("res-readonly"));
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
  CASE_EXPECT_FALSE(!!handle->get_locker("res-readonly"));

  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, unwritable_budget_survives_reload_and_unlocks) {
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
  recorder.check_writable_values = {0, 1};
  recorder.check_writable_script = {0, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  storage_ptr_type storage;
  auto task = test.run_task(
      "unwritable_prepare", std::chrono::seconds{4}, [handle, &storage](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("unwritable-budget", 2, std::chrono::milliseconds{60000}, {"res-a", "res-b"});
        *request.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval() =
            protobuf_from_chrono_duration(std::chrono::seconds{60});
        SSParticipatorTransactionPrepareRsp response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, storage)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  if (!storage) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto ctx = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(ctx, protobuf_to_system_clock(storage->resolve_timepoint())));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  CASE_EXPECT_EQ(1, storage->resolve_times());
  CASE_EXPECT_EQ(storage.get(), handle->get_locker("res-a").get());
  CASE_EXPECT_EQ(storage.get(), handle->get_locker("res-b").get());
  handle_type::snapshot_type snapshot;
  handle->dump(snapshot);
  handle->load(snapshot);
  auto restored = handle->get_locker("res-a");
  CASE_EXPECT_TRUE(!!restored);
  if (!restored) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  CASE_EXPECT_EQ(1, restored->resolve_times());
  CASE_EXPECT_EQ(0, handle->tick(ctx, protobuf_to_system_clock(restored->resolve_timepoint())));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 2; }));
  CASE_EXPECT_EQ(2, restored->resolve_times());
  CASE_EXPECT_EQ(0, query_calls);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  CASE_EXPECT_FALSE(!!handle->get_locker("res-a"));
  CASE_EXPECT_FALSE(!!handle->get_locker("res-b"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, writable_check_task_timeout_releases_lock) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  int check_calls = 0;
  int check_result = 0;
  auto vtable = atfw::component::memory::stl::make_strong_rc<handle_type::vtable_type>();
  vtable->check_writable = [&check_calls, &check_result](rpc::context& ctx, handle_type&,
                                                         bool& writable) -> rpc::result_code_type {
    ++check_calls;
    writable = false;
    check_result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::seconds{1}));
    RPC_RETURN_CODE(check_result);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type storage;
  auto task = test.run_task(
      "writable_timeout_prepare", std::chrono::seconds{4},
      [handle, &storage](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("writable-timeout", 1, std::chrono::milliseconds{60000}, {"res-timeout"});
        SSParticipatorTransactionPrepareRsp response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, storage)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  if (!storage) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto timeout_hook = task_manager::mock_create_task(
      [](gsl::string_view task_name, std::chrono::system_clock::duration& timeout) -> int {
        if (task_name.find("task_action_participator_resolve_transaction") != gsl::string_view::npos) {
          timeout = std::chrono::milliseconds{20};
        }
        return 0;
      });
  CASE_EXPECT_TRUE(static_cast<bool>(timeout_hook));
  auto ctx = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(ctx, protobuf_to_system_clock(storage->resolve_timepoint())));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [handle]() { return handle->get_running_transactions().empty(); }));
  CASE_EXPECT_EQ(1, check_calls);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, check_result);
  CASE_EXPECT_EQ(1, storage->resolve_times());
  CASE_EXPECT_FALSE(!!handle->get_locker("res-timeout"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  timeout_hook.reset();
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, unwritable_ack_exhausts_zero_budget_without_rpc) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  int ack_calls = 0;
  auto ack_rule = register_participator_ack_mock(test, "commit", &ack_calls);
  CASE_EXPECT_TRUE(!!ack_rule);
  participator_event_recorder recorder;
  recorder.check_writable_result = false;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task =
      test.run_task("unwritable_ack", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("unwritable-ack", 0, std::chrono::milliseconds{60000}, {"res-ack"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type storage;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, storage)));
        SSParticipatorTransactionCommitReq commit_request;
        SSParticipatorTransactionCommitRsp commit_response;
        commit_request.set_transaction_uuid("unwritable-ack");
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return handle->get_finished_transactions().empty(); }));
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_finish_running"));
  CASE_EXPECT_EQ(0, ack_calls);
  CASE_EXPECT_EQ(1, recorder.check_writable_calls);
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  CASE_EXPECT_FALSE(!!handle->get_locker("res-ack"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, stale_unwritable_check_preserves_reloaded_transaction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  handle_type::snapshot_type snapshot;
  auto vtable = recorder.make_vtable();
  vtable->check_writable = [&snapshot](rpc::context&, handle_type& owner, bool& writable) -> rpc::result_code_type {
    owner.load(snapshot);
    writable = false;
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type storage;
  auto task = test.run_task(
      "stale_unwritable_prepare", std::chrono::seconds{4},
      [handle, &storage](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("stale-unwritable", 1, std::chrono::milliseconds{60000}, {"res-stale"});
        SSParticipatorTransactionPrepareRsp response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, storage)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  if (!storage) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  handle->dump(snapshot);
  auto due = protobuf_to_system_clock(storage->resolve_timepoint());
  auto ctx = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(ctx, due));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  auto restored = handle->get_locker("res-stale");
  CASE_EXPECT_TRUE(!!restored);
  if (restored) {
    CASE_EXPECT_NE(storage.get(), restored.get());
    CASE_EXPECT_EQ(0, restored->resolve_times());
    CASE_EXPECT_EQ(due, protobuf_to_system_clock(restored->resolve_timepoint()));
  }
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, unwritable_timeout_unlocks_inflight_commit) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  bool entered = false;
  bool release = false;
  participator_event_recorder recorder;
  recorder.check_writable_result = false;
  auto vtable = recorder.make_vtable();
  vtable->do_event = [&entered, &release](rpc::context& ctx, handle_type&,
                                          const handle_type::storage_type&) -> rpc::result_code_type {
    entered = true;
    while (!release) {
      auto res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{1}));
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
    }
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type storage;
  auto prepare_task = test.run_task(
      "inflight_unwritable_prepare", std::chrono::seconds{4},
      [handle, &storage](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("inflight-unwritable", 1, std::chrono::milliseconds{60000}, {"res-inflight"});
        SSParticipatorTransactionPrepareRsp response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, storage)));
      });
  auto result = test.wait(prepare_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  if (!storage) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto commit_task = test.run_task("inflight_unwritable_commit", std::chrono::seconds{4},
                                   [handle](rpc::context& ctx) -> rpc::result_code_type {
                                     SSParticipatorTransactionCommitReq request;
                                     SSParticipatorTransactionCommitRsp response;
                                     request.set_transaction_uuid("inflight-unwritable");
                                     RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, request, response)));
                                   });
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&entered]() { return entered; }));
  auto ctx = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(ctx, protobuf_to_system_clock(storage->resolve_timepoint())));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
  CASE_EXPECT_FALSE(!!handle->get_locker("res-inflight"));
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                 storage->metadata().status());

  storage_ptr_type replacement;
  auto replacement_task = test.run_task(
      "inflight_unwritable_replacement", std::chrono::seconds{4},
      [handle, &replacement](rpc::context& context) -> rpc::result_code_type {
        auto request = make_prepare_request("replacement", 1, std::chrono::milliseconds{60000}, {"res-inflight"});
        SSParticipatorTransactionPrepareRsp response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(context, std::move(request), response, replacement)));
      });
  result = test.wait(replacement_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_TRUE(!!replacement);
  release = true;
  result = test.wait(commit_task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(replacement.get(), handle->get_locker("res-inflight").get());
  CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
  CASE_EXPECT_EQ(0, recorder.count("on_commited"));
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
  vtable->on_finish_running = [&recorder](rpc::context&, handle_type&,
                                          const handle_type::storage_type& storage) -> rpc::result_code_type {
    recorder.events.emplace_back("on_finish_running");
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED,
                   storage.metadata().status());
    RPC_RETURN_CODE(0);
  };
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
        auto request_a = make_prepare_request("part-uuid-seam-a", 3, std::chrono::milliseconds{20}, {"res-seam-a"});
        auto request_b = make_prepare_request("part-uuid-seam-b", 3, std::chrono::milliseconds{20}, {"res-seam-b"});
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
  auto batch_due = std::max(protobuf_to_system_clock(seam_a_iter->second.storage->resolve_timepoint()),
                            protobuf_to_system_clock(seam_b_iter->second.storage->resolve_timepoint()));

  // First tick: the collected timers hand over ownership to the task launch, which fails; tick
  // reports the injected error and everything collected must be re-armed with backoff.
  int tick_result = handle->tick(tick_context, batch_due);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC, tick_result);
  CASE_EXPECT_EQ(2, handle->get_running_transactions().size());
  CASE_EXPECT_TRUE(!!handle->get_locker("res-seam-a"));
  CASE_EXPECT_TRUE(!!handle->get_locker("res-seam-b"));
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
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
  CASE_EXPECT_FALSE(!!handle->get_locker("res-seam-a"));
  CASE_EXPECT_FALSE(!!handle->get_locker("res-seam-b"));
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
CASE_TEST(component_distributed_transaction_participator, relock_preserves_snapshot_and_releases_all_resources) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task =
      test.run_task("relock_snapshot", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("relock-snapshot", 3, std::chrono::milliseconds{60000}, {"res-a"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        if (!CASE_EXPECT_TRUE(!!output)) {
          RPC_RETURN_CODE(-1);
        }
        google::protobuf::RepeatedPtrField<std::string> resources;
        *resources.Add() = "res-a";
        *resources.Add() = "res-b";
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->lock(output, resources)));
        CASE_EXPECT_EQ(2, output->lock_resource_size());
        handle_type::snapshot_type snapshot;
        handle->dump(snapshot);
        handle->load(snapshot);
        CASE_EXPECT_TRUE(!!handle->get_locker("res-a"));
        CASE_EXPECT_TRUE(!!handle->get_locker("res-b"));
        SSParticipatorTransactionRejectReq reject_request;
        reject_request.set_transaction_uuid("relock-snapshot");
        SSParticipatorTransactionRejectRsp reject_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-a"));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-b"));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, commit_without_do_event_waits_for_ack) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  vtable->do_event = nullptr;
  vtable->on_finish_running = [](rpc::context&, handle_type& owner,
                                 const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                   storage.metadata().status());
    CASE_EXPECT_EQ(0, storage.lock_resource_size());
    CASE_EXPECT_FALSE(!!owner.get_locker("res-no-do"));
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  auto task =
      test.run_task("commit_no_do", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("commit-no-do", 3, std::chrono::milliseconds{60000}, {"res-no-do"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("commit-no-do");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        handle_type::snapshot_type snapshot;
        handle->dump(snapshot);
        CASE_EXPECT_EQ(1, snapshot.finished_transaction_size());
        if (snapshot.finished_transaction_size() == 1) {
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                         snapshot.finished_transaction(0).metadata().status());
        }
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_finished"));
  CASE_EXPECT_EQ(1, recorder.count("on_commited"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, running_committed_snapshot_preserves_state) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task = test.run_task(
      "commit_from_commiting", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("commit-from-commiting", 3, std::chrono::milliseconds{60000}, {"res-commiting"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        handle_type::snapshot_type snapshot;
        handle->dump(snapshot);
        CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
        if (snapshot.running_transaction_size() != 1) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
        }
        // A confirmed coordinator decision may precede completion of the local action.
        snapshot.mutable_running_transaction(0)->mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        handle->load(snapshot);
        CASE_EXPECT_TRUE(!!handle->get_locker("res-commiting"));
        if (handle->get_locker("res-commiting")) {
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                         handle->get_locker("res-commiting")->metadata().status());
        }
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("commit-from-commiting");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        CASE_EXPECT_FALSE(!!handle->get_locker("res-commiting"));
        handle->dump(snapshot);
        CASE_EXPECT_EQ(1, snapshot.finished_transaction_size());
        if (snapshot.finished_transaction_size() == 1) {
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                         snapshot.finished_transaction(0).metadata().status());
        }
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_commited"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, reject_waits_for_ack_after_callbacks) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  vtable->on_finish_running = [](rpc::context&, handle_type& owner,
                                 const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                   storage.metadata().status());
    CASE_EXPECT_EQ(0, storage.lock_resource_size());
    CASE_EXPECT_FALSE(!!owner.get_locker("res-local-reject"));
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  auto task = test.run_task(
      "local_reject_terminal", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("local-reject-terminal", 3, std::chrono::milliseconds{60000}, {"res-local-reject"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        SSParticipatorTransactionRejectReq reject_request;
        reject_request.set_transaction_uuid("local-reject-terminal");
        SSParticipatorTransactionRejectRsp reject_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        handle_type::snapshot_type snapshot;
        handle->dump(snapshot);
        CASE_EXPECT_EQ(1, snapshot.finished_transaction_size());
        if (snapshot.finished_transaction_size() == 1) {
          CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTING,
                         snapshot.finished_transaction(0).metadata().status());
        }
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_finished"));
  CASE_EXPECT_EQ(1, recorder.count("on_rejected"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, commit_retry_budget_survives_snapshot_reload) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  recorder.do_event_script = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT};
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task = test.run_task(
      "reload_commit_budget", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("reload-commit-budget", 2, std::chrono::milliseconds{60000}, {"res-budget"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("reload-commit-budget");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                       RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        handle_type::snapshot_type snapshot;
        handle->dump(snapshot);
        if (!CASE_EXPECT_EQ(1, snapshot.running_transaction_size())) {
          RPC_RETURN_CODE(-1);
        }
        CASE_EXPECT_EQ(1, snapshot.running_transaction(0).resolve_times());
        handle->load(snapshot);
        CASE_EXPECT_LT(atfw::util::time::time_utility::now(),
                       protobuf_to_system_clock(output->metadata().expire_timepoint()));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
        CASE_EXPECT_FALSE(!!handle->get_locker("res-budget"));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(2, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_commited"));
  CASE_EXPECT_EQ(0, recorder.count("on_rejected"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, commit_failure_keeps_direction_and_lock) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  recorder.do_event_script = {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, 0};
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task = test.run_task(
      "commit_direction_lock", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("commit-direction-lock", 2, std::chrono::milliseconds{60000}, {"res-commit"});
        auto contender = atfw::component::memory::stl::make_strong_rc<handle_type::storage_type>(request.storage());
        contender->mutable_metadata()->set_transaction_uuid("older-contender");
        contender->mutable_metadata()->mutable_prepare_timepoint()->set_seconds(
            request.storage().metadata().prepare_timepoint().seconds() - 1);
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("commit-direction-lock");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT,
                       RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        std::list<handle_type::storage_const_ptr_type> holders;
        std::vector<std::string> resources = {"res-commit"};
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       handle->check_lock(contender->metadata(), resources, holders));
        CASE_EXPECT_EQ(1, holders.size());
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
                       RPC_AWAIT_CODE_RESULT(handle->lock(contender, contender->lock_resource())));
        CASE_EXPECT_EQ(output.get(), handle->get_locker("res-commit").get());
        SSParticipatorTransactionRejectReq reject_request;
        reject_request.set_transaction_uuid("commit-direction-lock");
        SSParticipatorTransactionRejectRsp reject_response;
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED,
                       RPC_AWAIT_CODE_RESULT(handle->reject(ctx, reject_request, reject_response)));
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_FALSE(!!handle->get_locker("res-commit"));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(2, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_commited"));
  CASE_EXPECT_EQ(0, recorder.count("on_rejected"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, finish_callback_reentry_does_not_repeat_business_action) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  bool callback_reentered = false;
  vtable->on_finish_running = [&callback_reentered](rpc::context& ctx, handle_type& self,
                                                    const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                   storage.metadata().status());
    CASE_EXPECT_EQ(0, storage.lock_resource_size());
    CASE_EXPECT_FALSE(!!self.get_locker("res-finish-reentry"));
    if (callback_reentered) {
      RPC_RETURN_CODE(0);
    }
    callback_reentered = true;
    auto request = make_prepare_request(storage.metadata().transaction_uuid(), 3, std::chrono::milliseconds{60000});
    SSParticipatorTransactionPrepareRsp response;
    storage_ptr_type output;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(self.prepare(ctx, std::move(request), response, output)));
    CASE_EXPECT_EQ(&storage, output.get());
    SSParticipatorTransactionCommitReq commit_request;
    commit_request.set_transaction_uuid(storage.metadata().transaction_uuid());
    SSParticipatorTransactionCommitRsp commit_response;
    CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(self.commit(ctx, commit_request, commit_response)));
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  auto task = test.run_task(
      "finish_callback_reentry", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("finish-reentry", 3, std::chrono::milliseconds{60000}, {"res-finish-reentry"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("finish-reentry");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_TRUE(handle->get_running_transactions().empty());
        CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_TRUE(callback_reentered);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_start_running"));
  CASE_EXPECT_EQ(1, recorder.count("on_finished"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, stale_query_does_not_remove_reloaded_transaction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task = test.run_task(
      "prepare_before_reload", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("stale-query-reload", 3, std::chrono::milliseconds{60000}, {"res-reload"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  handle_type::snapshot_type snapshot;
  handle->dump(snapshot);
  if (!CASE_EXPECT_EQ(1, snapshot.running_transaction_size())) {
    test.stop();
    return;
  }
  auto query_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      atfw::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [handle, snapshot](const atfw::testing::ss_request_view&, google::protobuf::Message&) -> rpc::result_code_type {
        // Replace the snapshot while the real resolve RPC is awaiting its response.
        handle->load(snapshot);
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
      });
  CASE_EXPECT_TRUE(!!query_rule);
  auto old_storage = handle->get_locker("res-reload");
  handle->fire_resolve_custom_timer_for_unit_test(
      protobuf_to_system_clock(snapshot.running_transaction(0).resolve_timepoint()));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_query()));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
  CASE_EXPECT_TRUE(!!handle->get_locker("res-reload"));
  CASE_EXPECT_NE(old_storage.get(), handle->get_locker("res-reload").get());
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, stale_query_does_not_remove_wounded_transaction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  participator_event_recorder recorder;
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
  auto task = test.run_task(
      "prepare_before_wound", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("query-victim", 3, std::chrono::minutes{1}, {"query-resource"});
        request.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval()->set_seconds(60);
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  auto holder = handle->get_locker("query-resource");
  if (!CASE_EXPECT_TRUE(!!holder)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto query_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_query(),
      atfw::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name(),
      [handle, holder](const atfw::testing::ss_request_view& request,
                       google::protobuf::Message&) -> rpc::result_code_type {
        if (!CASE_EXPECT_TRUE(request.context != nullptr)) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
        }
        auto contender = make_prepare_request("query-contender", 3, std::chrono::minutes{2}, {"query-resource"});
        *contender.mutable_storage()->mutable_metadata()->mutable_prepare_timepoint() = protobuf_from_system_clock(
            protobuf_to_system_clock(holder->metadata().prepare_timepoint()) - std::chrono::seconds{1});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(
            PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_RESOURCE_PREEMPTED,
            RPC_AWAIT_CODE_RESULT(handle->prepare(*request.context, std::move(contender), response, output)));
        CASE_EXPECT_TRUE(response.reason().allow_retry());
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
      });
  CASE_EXPECT_TRUE(!!query_rule);
  handle->fire_resolve_custom_timer_for_unit_test(protobuf_to_system_clock(holder->resolve_timepoint()));
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
  CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_query()));
  CASE_EXPECT_EQ(holder.get(), handle->get_locker("query-resource").get());
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                 holder->metadata().status());
  CASE_EXPECT_EQ(1, holder->resolve_times());
  handle_type::snapshot_type snapshot;
  handle->dump(snapshot);
  CASE_EXPECT_EQ(1, snapshot.running_transaction_size());
  CASE_EXPECT_EQ(1, snapshot.wounded_transaction_uuid_size());
  CASE_EXPECT_EQ(0, snapshot.finished_transaction_size());
  CASE_EXPECT_TRUE(handle->has_resolve_custom_timer_for_unit_test());
  CASE_EXPECT_EQ(0, recorder.count("on_finish_running"));
  handle->load(snapshot);
  holder = handle->get_locker("query-resource");
  if (!CASE_EXPECT_TRUE(!!holder)) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  CASE_EXPECT_EQ(1, holder->resolve_times());
  auto reject_rule = test.ss().mock_error(rpc::transaction::packer::get_full_name_of_reject(),
                                          PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  CASE_EXPECT_TRUE(!!reject_rule);
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return handle->get_running_transactions().empty(); }));
  CASE_EXPECT_EQ(2, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject()));
  CASE_EXPECT_EQ(3, holder->resolve_times());
  CASE_EXPECT_FALSE(!!handle->get_locker("query-resource"));
  CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
  CASE_EXPECT_EQ(1, recorder.count("on_finish_running"));
  CASE_EXPECT_EQ(0, recorder.count("on_finished"));
  CASE_EXPECT_EQ(0, test.ss().calls(rpc::transaction::packer::get_full_name_of_reject_participator()));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, stale_commit_does_not_consume_reloaded_transaction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  vtable->do_event = [](rpc::context&, handle_type& self, const handle_type::storage_type&) -> rpc::result_code_type {
    handle_type::snapshot_type snapshot;
    self.dump(snapshot);
    self.load(snapshot);
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  auto task = test.run_task(
      "stale_commit_reload", std::chrono::seconds{4}, [handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request =
            make_prepare_request("stale-commit-reload", 3, std::chrono::milliseconds{60000}, {"res-local-reload"});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("stale-commit-reload");
        SSParticipatorTransactionCommitRsp commit_response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
        CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
        CASE_EXPECT_TRUE(!!handle->get_locker("res-local-reload"));
        CASE_EXPECT_NE(output.get(), handle->get_locker("res-local-reload").get());
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, recorder.count("on_finished"));
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, stale_on_finished_does_not_finalize_reloaded_transaction) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  int finish_calls = 0;
  handle_type::snapshot_type snapshot;
  vtable->on_finished = [&finish_calls, &snapshot](rpc::context&, handle_type& owner,
                                                   const handle_type::storage_type& storage) -> rpc::result_code_type {
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                   storage.metadata().status());
    if (++finish_calls == 1) {
      owner.dump(snapshot);
      if (!CASE_EXPECT_EQ(1, snapshot.finished_transaction_size())) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
      }
      protobuf_copy_message(*snapshot.mutable_finished_transaction(0)->mutable_resolve_timepoint(),
                            storage.metadata().expire_timepoint());
      owner.load(snapshot);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    }
    RPC_RETURN_CODE(0);
  };
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");
  storage_ptr_type old_output;
  auto task = test.run_task(
      "stale_finish", std::chrono::seconds{4}, [handle, &old_output](rpc::context& ctx) -> rpc::result_code_type {
        auto request = make_prepare_request("stale-finish", 2, std::chrono::milliseconds{60000});
        SSParticipatorTransactionPrepareRsp response;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, old_output)));
        SSParticipatorTransactionCommitReq commit_request;
        commit_request.set_transaction_uuid("stale-finish");
        SSParticipatorTransactionCommitRsp commit_response;
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, result.result_code);
  CASE_EXPECT_EQ(0, recorder.count("on_commited"));
  auto current = handle->get_finished_transactions().find("stale-finish");
  if (!CASE_EXPECT_TRUE(current != handle->get_finished_transactions().end())) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }
  auto new_output = current->second;
  CASE_EXPECT_NE(old_output.get(), new_output.get());
  CASE_EXPECT_EQ(0, new_output->resolve_times());
  CASE_EXPECT_EQ(protobuf_to_system_clock(snapshot.finished_transaction(0).resolve_timepoint()),
                 handle->get_resolve_custom_timer_timepoint_for_unit_test());
  int ack_calls = 0;
  auto ack_rule = register_participator_ack_mock(test, "commit", &ack_calls);
  CASE_EXPECT_TRUE(!!ack_rule);
  CASE_EXPECT_TRUE(drive_handle(test, handle, [handle]() { return handle->get_finished_transactions().empty(); }));
  CASE_EXPECT_EQ(2, finish_calls);
  CASE_EXPECT_EQ(1, recorder.count("do_event"));
  CASE_EXPECT_EQ(1, recorder.count("on_commited"));
  CASE_EXPECT_EQ(1, ack_calls);
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING,
                 old_output->metadata().status());
  CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED,
                 new_output->metadata().status());
  CASE_EXPECT_EQ(0, test.stop());
}

CASE_TEST(component_distributed_transaction_participator, stale_ack_does_not_replace_reloaded_query_timer) {
  for (int32_t acknowledge_result :
       {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, PROJECT_NAMESPACE_ID::err::EN_SUCCESS}) {
    atfw::testing::runtime test;
    atfw::testing::runtime_options options;
    options.features = {atfw::testing::feature::ss};
    CASE_EXPECT_EQ(0, test.start(options));
    if (!test.is_running()) {
      return;
    }
    CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));
    participator_event_recorder recorder;
    auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(recorder.make_vtable(), "p");
    handle_type::snapshot_type snapshot;
    storage_ptr_type output;
    auto task = test.run_task(
        "prepare_stale_ack", std::chrono::seconds{4},
        [handle, &snapshot, &output](rpc::context& ctx) -> rpc::result_code_type {
          auto request =
              make_prepare_request("stale-ack-reload", 3, std::chrono::milliseconds{60000}, {"res-ack-reload"});
          SSParticipatorTransactionPrepareRsp response;
          CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request), response, output)));
          handle->dump(snapshot);
          SSParticipatorTransactionCommitReq commit_request;
          commit_request.set_transaction_uuid("stale-ack-reload");
          SSParticipatorTransactionCommitRsp commit_response;
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle->commit(ctx, commit_request, commit_response)));
        });
    auto result = test.wait(task, std::chrono::seconds{8});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
    if (!CASE_EXPECT_EQ(1, snapshot.running_transaction_size())) {
      test.stop();
      return;
    }
    auto ack_rule = test.ss().mock(
        rpc::transaction::packer::get_full_name_of_commit_participator(),
        atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name(),
        atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name(),
        [handle, snapshot, acknowledge_result](const atfw::testing::ss_request_view& request,
                                               google::protobuf::Message& response) -> rpc::result_code_type {
          const auto& typed_request =
              static_cast<const atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq&>(request.body);
          auto& typed_response =
              static_cast<atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp&>(response);
          protobuf_copy_message(*typed_response.mutable_metadata(), typed_request.metadata());
          protobuf_copy_message(*typed_response.mutable_metadata()->mutable_finish_timepoint(),
                                typed_request.metadata().prepare_timepoint());
          handle->load(snapshot);
          RPC_RETURN_CODE(acknowledge_result);
        });
    CASE_EXPECT_TRUE(!!ack_rule);
    handle->fire_resolve_custom_timer_for_unit_test(handle->get_resolve_custom_timer_timepoint_for_unit_test());
    CASE_EXPECT_TRUE(
        dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") == 1; }));
    CASE_EXPECT_EQ(1, test.ss().calls(rpc::transaction::packer::get_full_name_of_commit_participator()));
    CASE_EXPECT_TRUE(handle->get_finished_transactions().empty());
    CASE_EXPECT_EQ(1, handle->get_running_transactions().size());
    CASE_EXPECT_TRUE(!!output);
    if (output) {
      CASE_EXPECT_EQ(0, output->metadata().finish_timepoint().seconds());
      CASE_EXPECT_EQ(0, output->metadata().finish_timepoint().nanos());
    }
    auto locker = handle->get_locker("res-ack-reload");
    CASE_EXPECT_TRUE(!!locker);
    if (locker) {
      CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                     locker->metadata().status());
    }
    CASE_EXPECT_EQ(protobuf_to_system_clock(snapshot.running_transaction(0).resolve_timepoint()),
                   handle->get_resolve_custom_timer_timepoint_for_unit_test());
    CASE_EXPECT_EQ(0, test.stop());
  }
}

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
        // 同一 finished 对象保持待 ACK 状态，不允许重新进入 running 或再次执行 do_event。
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

  // 显式推进同一批次的两个到期事务，长重试间隔避免断言期间进入下一轮。
  auto task = test.run_task(
      "writable_batch_seed", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
        auto request_a = make_prepare_request("part-uuid-wb-a", 2, std::chrono::milliseconds{60000});
        auto request_b = make_prepare_request("part-uuid-wb-b", 2, std::chrono::milliseconds{60000});
        *request_a.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval() =
            protobuf_from_chrono_duration(std::chrono::seconds{60});
        *request_b.mutable_storage()->mutable_configure()->mutable_resolve_retry_interval() =
            protobuf_from_chrono_duration(std::chrono::seconds{60});
        SSParticipatorTransactionPrepareRsp response;
        storage_ptr_type output;
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_a), response, output)));
        CASE_EXPECT_EQ(0, RPC_AWAIT_CODE_RESULT(handle->prepare(ctx, std::move(request_b), response, output)));
        RPC_RETURN_CODE(0);
      });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);

  auto batch_due = std::chrono::system_clock::time_point::min();
  for (const auto& entry : handle->get_running_transactions()) {
    batch_due = std::max(batch_due, protobuf_to_system_clock(entry.second.storage->resolve_timepoint()));
  }
  auto tick_context = atfw::testing::make_context();
  CASE_EXPECT_EQ(0, handle->tick(tick_context, batch_due));
  // A 完成本地动作，B 的检查失败；随后 A 的立即到期 acknowledge 也遇到不可写。
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") >= 2; }));
  CASE_EXPECT_EQ(1, queried_uuids.size());
  CASE_EXPECT_EQ(std::string("part-uuid-wb-a"), queried_uuids[0]);
  CASE_EXPECT_EQ(0, handle->get_running_transactions().count("part-uuid-wb-a"));
  CASE_EXPECT_EQ(1, handle->get_running_transactions().count("part-uuid-wb-b"));
  // 只给未处理的 B 增加一次重试计数，已经完成的 A 不再增加 query 阶段的重试计数。
  auto wb_b_iter = handle->get_running_transactions().find("part-uuid-wb-b");
  CASE_EXPECT_TRUE(wb_b_iter != handle->get_running_transactions().end());
  if (wb_b_iter == handle->get_running_transactions().end()) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(1, wb_b_iter->second.storage->resolve_times());
  // 批次闸门保持不可写时，A 的 acknowledge 也一并延后
  CASE_EXPECT_EQ(1, handle->get_finished_transactions().count("part-uuid-wb-a"));
  CASE_EXPECT_EQ(1, recorder.count("do_event"));

  // 恢复可写性并推进剩余条目的下一次恢复时间。
  recorder.check_writable_result = true;
  auto retry_due = protobuf_to_system_clock(wb_b_iter->second.storage->resolve_timepoint());
  for (const auto& entry : handle->get_finished_transactions()) {
    CASE_EXPECT_EQ(1, entry.second->resolve_times());
    retry_due = std::max(retry_due, protobuf_to_system_clock(entry.second->resolve_timepoint()));
  }
  CASE_EXPECT_EQ(0, handle->tick(tick_context, retry_due));
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

// ============ force_commit undo：内部 UUID 缺失视为非法，不触发 undo_event ============
CASE_TEST(component_distributed_transaction_participator, force_commit_undo_rejects_missing_inner_uuid) {
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
      "undo_missing_inner_uuid", std::chrono::seconds{4}, [&handle](rpc::context& ctx) -> rpc::result_code_type {
    // 外部 UUID 存在、内部 metadata UUID 为空：不能绕过一致性校验进入 undo_event
    SSParticipatorTransactionRejectReq undo_request;
    undo_request.set_transaction_uuid("part-uuid-undo-empty-inner");
    auto* undo_storage = undo_request.mutable_storage();
    undo_storage->mutable_configure()->set_force_commit(true);
    SSParticipatorTransactionRejectRsp undo_response;
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM,
                   RPC_AWAIT_CODE_RESULT(handle->reject(ctx, undo_request, undo_response)));
    RPC_RETURN_CODE(0);
  });
  auto result = test.wait(task, std::chrono::seconds{8});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, recorder.undo_calls);
  CASE_EXPECT_EQ(0, recorder.count("undo_event"));

  CASE_EXPECT_EQ(0, test.stop());
}

// ============ resolve 任务退出：ACK 循环退出后不再处理 pending 条目，不增加查询重试计数 ============
CASE_TEST(component_distributed_transaction_participator, exiting_ack_loop_skips_pending_batch) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  CASE_EXPECT_TRUE(dt_test::inject_coordinators(test, {0x1B0001}));

  participator_event_recorder recorder;
  auto vtable = recorder.make_vtable();
  auto handle = atfw::component::memory::stl::make_strong_rc<handle_type>(vtable, "p");

  // 1 个 finished（回调已完成，直接 ACK）+ 1 个 running（到期查询）事务
  handle_type::snapshot_type snapshot;
  auto expiry = atfw::util::time::time_utility::now() + std::chrono::seconds{60};
  {
    auto finished_request = make_prepare_request("exiting-finished", 2, std::chrono::seconds{60});
    auto* finished_storage = snapshot.add_finished_transaction();
    *finished_storage = finished_request.storage();
    finished_storage->mutable_metadata()->set_status(
        EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITING);
    finished_storage->set_finished_callback_completed(true);
    *finished_storage->mutable_resolve_timepoint() = protobuf_from_system_clock(expiry);
    auto running_request = make_prepare_request("exiting-running", 2, std::chrono::seconds{60}, {"exiting-res"});
    auto* running_storage = snapshot.add_running_transaction();
    *running_storage = running_request.storage();
    *running_storage->mutable_resolve_timepoint() = protobuf_from_system_clock(expiry);
  }
  handle->load(snapshot);

  int query_calls = 0;
  auto query_rule = register_query_mock(test, EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                                        &query_calls);
  CASE_EXPECT_TRUE(!!query_rule);
  // running 事务查询始终 PREPARED，重试次数超过上限后进入拒绝流程：需要 reject ACK 成功才能清理
  int reject_ack_calls = 0;
  auto reject_ack_rule = register_participator_ack_mock(test, "reject", &reject_ack_calls);
  CASE_EXPECT_TRUE(!!reject_ack_rule);

  // ACK RPC 挂起，直到测试放行
  bool ack_entered = false;
  bool release_ack = false;
  auto ack_rule = test.ss().mock(
      rpc::transaction::packer::get_full_name_of_commit_participator(),
      atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name(),
      atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name(),
      [&ack_entered, &release_ack](const atfw::testing::ss_request_view& view,
                                   google::protobuf::Message& response) -> rpc::result_code_type {
        ack_entered = true;
        while (!release_ack) {
          if (nullptr == view.context) {
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          }
          auto res = RPC_AWAIT_CODE_RESULT(rpc::wait(*view.context, std::chrono::milliseconds{1}));
          if (res < 0) {
            RPC_RETURN_CODE(res);
          }
        }
        const auto& request =
            static_cast<const atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq&>(view.body);
        auto& output = static_cast<atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp&>(response);
        *output.mutable_metadata() = request.metadata();
        output.mutable_metadata()->set_status(
            EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
        RPC_RETURN_CODE(0);
      });
  CASE_EXPECT_TRUE(!!ack_rule);

  // resolve 任务使用注入的短超时：ACK 挂起期间任务被超时杀死
  task_manager::mock_create_task_clear();
  auto timeout_hook = task_manager::mock_create_task(
      [](gsl::string_view demangled_task_type_name, std::chrono::system_clock::duration& timeout) -> int {
        if (demangled_task_type_name.find("task_action_participator_resolve_transaction") != gsl::string_view::npos) {
          timeout = std::chrono::milliseconds{30};
        }
        return 0;
      });
  CASE_EXPECT_TRUE(static_cast<bool>(timeout_hook));

  handle->fire_resolve_custom_timer_for_unit_test(expiry);
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&ack_entered]() { return ack_entered; }));

  // 等待任务退出并完成 rearm：pending 条目保持原状，没有发起查询，也没有增加重试计数
  CASE_EXPECT_TRUE(dt_test::wait_for(test, [&recorder]() { return recorder.count("on_resolve_task_finished") >= 1; }));
  CASE_EXPECT_EQ(0, query_calls);
  auto current_running = handle->get_running_transactions().find("exiting-running");
  if (CASE_EXPECT_TRUE(current_running != handle->get_running_transactions().end())) {
    CASE_EXPECT_EQ(0, current_running->second.storage->resolve_times());
    CASE_EXPECT_EQ(EnDistibutedTransactionStatus::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED,
                   current_running->second.storage->metadata().status());
  }
  // ACK 条目同样未完成：保留在 finished 集合等待重试
  CASE_EXPECT_EQ(1, handle->get_finished_transactions().size());

  // 解除挂起并移除钩子后，恢复流程正常完成
  release_ack = true;
  timeout_hook.reset();
  task_manager::mock_create_task_clear();
  CASE_EXPECT_TRUE(drive_handle(test, handle, [&handle, &query_calls]() {
    return query_calls >= 1 && handle->get_running_transactions().empty() &&
           handle->get_finished_transactions().empty();
  }));
  CASE_EXPECT_GE(query_calls, 1);
  CASE_EXPECT_FALSE(!!handle->get_locker("exiting-res"));
  CASE_EXPECT_FALSE(handle->has_resolve_custom_timer_for_unit_test());

  CASE_EXPECT_EQ(0, test.stop());
}
