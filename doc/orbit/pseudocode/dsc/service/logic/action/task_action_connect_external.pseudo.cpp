#include "task_action_connect_external.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_DUPLICATE_ID = 15;

static unsigned long long extract_connection_handle(rpc_context_t& rpc_context) {
  (void)rpc_context;
  return 9001;
}

static const char* current_controller_route_key(runtime_handle_t& runtime) {
  (void)runtime;
  return "dsc://region-cn-east/controller-a";
}

static rpc_result_t write_connect_external_response(rpc_context_t& rpc_context) {
  (void)rpc_context;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_connect_external(runtime_handle_t& runtime,
                                              rpc_context_t& rpc_context,
                                              const ConnectExternalReq& request,
                                              session::session_router& session_router) {
  if (!request.has_session() || 0 == request.unique_id()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  auto connect_result = session_router.connect(
      request.unique_id(), extract_connection_handle(rpc_context), current_controller_route_key(runtime));
  if (connect_result == -2) {
    return build_rpc_error(ERROR_CODE_DUPLICATE_ID);
  }

  if (connect_result != 0) {
    return build_rpc_error(connect_result);
  }

  return write_connect_external_response(rpc_context);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit