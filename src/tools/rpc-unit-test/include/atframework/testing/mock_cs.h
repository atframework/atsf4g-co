// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atgateway/protocol/libatgw_server_protocol.h>
#include <protocol/extension/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/runtime.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "dispatcher/cs_msg_dispatcher.h"

namespace atframework {
namespace testing {

class mock_cs;

// One captured downstream send at the unified gateway-send boundary.
struct ATFW_UTIL_SYMBOL_VISIBLE cs_downstream_record {
  enum class op_type : int32_t {
    post = 0,
    kickoff,
    set_router,
    broadcast,
  };

  op_type op = op_type::post;
  uint64_t node_id = 0;
  uint64_t session_id = 0;
  // Multi-session broadcast only: requested session id list (observability; the production fallback
  // intentionally ignores it, matching pre-seam behavior).
  std::vector<uint64_t> session_ids;
  // Parsed atfw::gateway::server_message exactly as it would have gone onto the bus.
  atfw::gateway::server_message message;
};

// A simulated client/gateway session. Upstream messages are built in the real wire format
// (message_t with type kAtGateway and a serialized atfw::gateway::server_message body) and injected
// through the real cs_msg_dispatcher::dispatch, so session_manager, unpack and task action paths
// all run exactly as in production.
class RPC_UNIT_TEST_API mock_client {
 public:
  mock_client() = default;

  bool empty() const noexcept { return nullptr == engine_; }
  explicit operator bool() const noexcept { return !empty(); }

  uint64_t node_id() const noexcept { return node_id_; }
  uint64_t session_id() const noexcept { return session_id_; }

  // kAddSession: creates the real session through session_manager.
  int32_t add(gsl::string_view client_ip = "127.0.0.1", uint32_t client_port = 0) const;
  // kPost: packs and injects a typed CSMsg upstream.
  int32_t post(const atframework::CSMsg &msg) const;
  // kRemoveSession: closes the session through the real remove path (including the logout task).
  int32_t remove() const;
  // kSetRouterRsp: completes a set-router request/response chain.
  int32_t set_router_rsp(int32_t error_code, uint64_t target_service_id, gsl::string_view target_service_name) const;

 private:
  friend class mock_cs;
  mock_client(mock_cs *engine, uint64_t node_id, uint64_t session_id);

  int32_t dispatch_message(atfw::gateway::server_message &msg) const;

  mock_cs *engine_ = nullptr;
  uint64_t node_id_ = 0;
  uint64_t session_id_ = 0;
};

// Mock CS engine. Downstream sends are captured at the cs_msg_dispatcher gateway-send hook and
// parsed into typed records. Following the default policy, unregistered downstream sends are
// captured-and-dropped with success (they are one-way notifications); error injection is explicit.
class RPC_UNIT_TEST_API mock_cs {
 public:
  using op_type = cs_downstream_record::op_type;

  mock_cs();
  ~mock_cs();

  mock_cs(const mock_cs &) = delete;
  mock_cs &operator=(const mock_cs &) = delete;

  bool is_active() const noexcept;

  mock_client create_client(uint64_t node_id, uint64_t session_id) noexcept {
    return mock_client{this, node_id, session_id};
  }

  // History
  size_t call_count() const noexcept { return calls_.size(); }
  const cs_downstream_record *call_at(size_t index) const;
  size_t calls(op_type op) const;
  // Downstream posts/kickoffs addressed to one session.
  size_t calls_to(uint64_t session_id) const;
  void clear_history() noexcept { calls_.clear(); }

  // Error injection: while set, every captured send is reported with this result code.
  void set_send_error(int32_t result_code) noexcept { send_error_ = result_code; }
  void clear_send_error() noexcept { send_error_ = 0; }

  std::string get_diagnostic() const { return diagnostic_; }

 private:
  friend class runtime;
  void bind();
  void unbind();

  bool on_send(const cs_msg_dispatcher::unit_test_gateway_send_request &request, int32_t &result_code);

  bool bound_ = false;
  int32_t send_error_ = 0;
  std::string diagnostic_;
  std::deque<cs_downstream_record> calls_;
};

}  // namespace testing
}  // namespace atframework
