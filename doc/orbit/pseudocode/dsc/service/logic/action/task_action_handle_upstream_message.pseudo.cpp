#include "task_action_handle_upstream_message.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_DS_NOT_FOUND = 16;
constexpr int ERROR_CODE_UPSTREAM_BUFFER_FAILED = 17;

static long long get_now_ms(runtime_handle_t& runtime) {
  (void)runtime;
  return 1000;
}

static rpc_result_t write_handle_upstream_message_response(rpc_context_t& rpc_context) {
  (void)rpc_context;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_handle_upstream_message(runtime_handle_t& runtime,
                                                     rpc_context_t& rpc_context,
                                                     const ForwardFromDSReq& request,
                                                     session::session_router& session_router,
                                                     external_upstream_sender& external_upstream_sender,
                                                     forwarding::upstream_buffer_store& upstream_buffer_store) {
  if (!request.has_ds() || 0 == request.dsa_id() || 0 == request.ds_id() || 0 == request.ack_seq() ||
      nullptr == request.payload()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  session::ds_composite_key_t ds_key;
  ds_key.dsa_id = request.dsa_id();
  ds_key.ds_id = request.ds_id();

  auto owner_unique_id = session_router.find_owner_unique_id(ds_key);
  if (0 == owner_unique_id) {
    return build_rpc_error(ERROR_CODE_DS_NOT_FOUND);
  }

  forwarding::buffered_upstream_message_t message;
  message.seq = request.ack_seq();
  message.dsa_id = request.dsa_id();
  message.ds_id = request.ds_id();
  message.payload = request.payload();
  message.occupied = true;

  if (session_router.is_connected(owner_unique_id)) {
    auto send_result = external_upstream_sender.send_upstream_message(owner_unique_id, message);
    if (send_result == 0) {
      return write_handle_upstream_message_response(rpc_context);
    }
  }

  auto buffer_result = upstream_buffer_store.buffer_offline_message(owner_unique_id, message, get_now_ms(runtime));
  if (buffer_result != 0 && buffer_result != 1) {
    return build_rpc_error(ERROR_CODE_UPSTREAM_BUFFER_FAILED);
  }

  return write_handle_upstream_message_response(rpc_context);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit