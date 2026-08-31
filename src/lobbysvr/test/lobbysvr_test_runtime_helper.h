// Copyright 2026 atframework
//
// Shared runtime drivers and downstream-post helpers for lobbysvr offline unit tests.
//   - run_sync_task / pump_rounds: task execution and settling pumps on atfw::testing::runtime;
//   - find_stream_post_indices: ordered indices of downstream stream posts of one rpc name for one session,
//     used to assert the relative order of two pushes carried by the same flush;
//   - flush_pending_chat_messages: drives user_chat_manager::global_tick while respecting its 100ms wall-clock
//     dedup window, then pumps to let the spawned push task finish.
//
// Scenario inputs and expectations stay in the case files; this header only carries mechanics.

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/extension/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/runtime.h>

#include <gsl/select-gsl.h>

#include <rpc/rpc_context.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "frame/test_macros.h"
#include "logic/chat/user_chat_manager.h"

namespace lobbysvr_test {

inline bool run_sync_task(atfw::testing::runtime& test, const char* name,
                          std::function<rpc::result_code_type(rpc::context&)> fn) {
  auto task = test.run_task(name, std::chrono::seconds{2}, std::move(fn));
  if (task.empty()) {
    CASE_MSG_INFO() << name << " start failed: " << task.get_diagnostic() << '\n';
    return false;
  }
  auto result = test.wait(task, std::chrono::seconds{5});
  if (!result.task_exited || 0 != result.result_code) {
    CASE_MSG_INFO() << name << " failed, exited=" << result.task_exited << " code=" << result.result_code << '\n';
    return false;
  }
  return true;
}

// Settling helper for work with no observable condition (e.g. letting fire-and-forget child tasks finish before
// teardown). Never use a fixed count as business evidence; prefer pumping until a real predicate holds.
inline void pump_rounds(atfw::testing::runtime& test, int count) {
  for (int i = 0; i < count; ++i) {
    test.pump_once();
  }
}

// Indices of every downstream stream post of one rpc name addressed to one session, in send order (used to
// assert the relative order of a dirty push and the chat sync that carried it).
inline std::vector<size_t> find_stream_post_indices(atfw::testing::runtime& test, uint64_t session_id,
                                                    gsl::string_view rpc_full_name) {
  std::vector<size_t> ret;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto* record = test.cs().call_at(i);
    if (nullptr == record || atfw::testing::cs_downstream_record::op_type::post != record->op ||
        record->session_id != session_id) {
      continue;
    }
    atframework::CSMsg cs_msg;
    if (!cs_msg.ParseFromString(record->message.body().post().content())) {
      continue;
    }
    if (cs_msg.head().has_rpc_stream() && rpc_full_name == cs_msg.head().rpc_stream().rpc_name()) {
      ret.push_back(i);
    }
  }
  return ret;
}

// user_chat_manager::global_tick only flushes once per 100ms wall window; sleep first so consecutive flushes in one
// case are not deduplicated, then pump to let the spawned push task finish.
inline void flush_pending_chat_messages(atfw::testing::runtime& test) {
  std::this_thread::sleep_for(std::chrono::milliseconds{110});
  static_cast<void>(run_sync_task(test, "chat.global_tick", [](rpc::context& ctx) -> rpc::result_code_type {
    user_chat_manager::global_tick(ctx);
    RPC_RETURN_CODE(0);
  }));
  pump_rounds(test, 6);
}

}  // namespace lobbysvr_test
