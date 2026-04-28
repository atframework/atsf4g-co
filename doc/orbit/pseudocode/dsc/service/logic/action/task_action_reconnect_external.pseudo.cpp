#include "task_action_reconnect_external.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_DUPLICATE_SESSION = 11;
constexpr int ERROR_CODE_REPLAY_FAILED = 12;
constexpr int ERROR_CODE_RESUME_FAILED = 13;
constexpr unsigned long long MAX_REPLAY_BATCH = 32;

static bool has_duplicate_live_session(session::session_router& session_router, unsigned long long unique_id) {
  return session_router.is_connected(unique_id);
}

static unsigned long long get_inbound_connection_handle(const rpc_context_t& rpc_context) {
  return rpc_context.inbound_connection_handle;
}

static const char* get_controller_route_key(const rpc_context_t& rpc_context) {
  return rpc_context.controller_route_key;
}

static rpc_result_t write_reconnect_response(rpc_context_t& rpc_context,
                                             const forwarding::buffered_upstream_message_t replay_messages[],
                                             unsigned long long replay_count) {
  rpc_context.replay_message_count = replay_count < MAX_REPLAY_BATCH ? replay_count : MAX_REPLAY_BATCH;
  for (unsigned long long index = 0; index < rpc_context.replay_message_count; ++index) {
    rpc_context.replay_seq[index] = replay_messages[index].seq;
    rpc_context.replay_dsa_id[index] = replay_messages[index].dsa_id;
    rpc_context.replay_ds_id[index] = replay_messages[index].ds_id;
    rpc_context.replay_payload[index] = replay_messages[index].payload;
  }

  return 0;
}

}  // namespace

rpc_result_t run_task_action_reconnect_external(runtime_handle_t& runtime,
                                                rpc_context_t& rpc_context,
                                                const ReconnectExternalReq& request,
                                                session::session_router& session_router,
                                                forwarding::reconnect_replay& replay_engine) {
  if (!request.has_session() || 0 == request.unique_id()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  if (has_duplicate_live_session(session_router, request.unique_id())) {
    return build_rpc_error(ERROR_CODE_DUPLICATE_SESSION);
  }

  forwarding::buffered_upstream_message_t replay_messages[MAX_REPLAY_BATCH] = {};
  unsigned long long replay_count = 0;
  auto replay_result = replay_engine.collect_replay_messages(
      request.unique_id(), request.last_received_seq(), replay_messages, MAX_REPLAY_BATCH, replay_count);
  if (replay_result != 0) {
    return build_rpc_error(ERROR_CODE_REPLAY_FAILED);
  }

  auto resume_result = resume_external_session_after_reconnect(runtime, rpc_context, request, session_router);
  if (resume_result != 0) {
    return build_rpc_error(resume_result);
  }

  return write_reconnect_response(rpc_context, replay_messages, replay_count);
}

int resume_external_session_after_reconnect(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const ReconnectExternalReq& request,
                                            session::session_router& session_router) {
  if (0 == request.unique_id() || 0 == get_inbound_connection_handle(rpc_context) ||
      nullptr == get_controller_route_key(rpc_context)) {
    return ERROR_CODE_INVALID_ARGUMENT;
  }

  (void)runtime;
  auto reconnect_result = session_router.reconnect(request.unique_id(),
                                                   get_inbound_connection_handle(rpc_context),
                                                   get_controller_route_key(rpc_context),
                                                   request.last_received_seq());
  if (reconnect_result != 0) {
    return ERROR_CODE_RESUME_FAILED;
  }

  rpc_context.reconnect_resumed = true;
  rpc_context.reconnect_unique_id = request.unique_id();
  return 0;
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit