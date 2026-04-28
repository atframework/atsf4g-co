#include "task_action_send_to_ds.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_PERMISSION_DENIED = 13;
constexpr int ERROR_CODE_FORWARD_TO_DS_FAILED = 14;

static rpc_result_t write_send_to_ds_response(rpc_context_t& rpc_context,
                                              const session::ds_composite_key_t& ds_key,
                                              bool require_ack,
                                              unsigned long long ack_seq) {
  rpc_context.send_to_ds_response_written = true;
  rpc_context.send_to_ds_dsa_id = ds_key.dsa_id;
  rpc_context.send_to_ds_ds_id = ds_key.ds_id;
  rpc_context.send_to_ds_require_ack = require_ack;
  rpc_context.send_to_ds_ack_seq = ack_seq;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_send_to_ds(runtime_handle_t& runtime,
                                        rpc_context_t& rpc_context,
                                        const SendToDSReq& request,
                                        session::session_router& session_router,
                                        forwarding::reliable_forwarder& reliable_forwarder) {
  (void)runtime;
  if (!request.has_session() || 0 == request.unique_id() || 0 == request.dsa_id() || 0 == request.ds_id() ||
      nullptr == request.payload()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  session::ds_composite_key_t ds_key;
  ds_key.dsa_id = request.dsa_id();
  ds_key.ds_id = request.ds_id();

  if (!session_router.validate_ds_owner(request.unique_id(), ds_key)) {
    return build_rpc_error(ERROR_CODE_PERMISSION_DENIED);
  }

  auto forward_result = reliable_forwarder.forward_to_ds(
      request.unique_id(), ds_key, request.payload(), request.require_ack(), request.ack_seq());
  if (forward_result != 0) {
    return build_rpc_error(ERROR_CODE_FORWARD_TO_DS_FAILED);
  }

  return write_send_to_ds_response(rpc_context, ds_key, request.require_ack(), request.ack_seq());
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit