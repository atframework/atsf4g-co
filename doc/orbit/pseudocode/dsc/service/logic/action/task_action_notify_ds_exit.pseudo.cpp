#include "task_action_notify_ds_exit.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_DS_NOT_FOUND = 16;

static void notify_owner_ds_exit(runtime_handle_t& runtime,
                                 unsigned long long owner_unique_id,
                                 const NotifyDSExitReq& request) {
  (void)runtime;
  (void)owner_unique_id;
  (void)request;
}

static rpc_result_t write_notify_ds_exit_response(rpc_context_t& rpc_context) {
  (void)rpc_context;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_notify_ds_exit(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const NotifyDSExitReq& request,
                                            registry::disconnect_cleanup& disconnect_cleanup,
                                            session::session_router& session_router,
                                            registry::agent_registry& agent_registry) {
  if (!request.has_ds() || 0 == request.dsa_id() || 0 == request.ds_id()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  session::ds_composite_key_t ds_key;
  ds_key.dsa_id = request.dsa_id();
  ds_key.ds_id = request.ds_id();

  auto cleanup_result = disconnect_cleanup.cleanup_ds_exit(session_router, ds_key);
  if (!cleanup_result.found_owner) {
    return build_rpc_error(ERROR_CODE_DS_NOT_FOUND);
  }

  registry::load_snapshot_t load;
  load.cpu_used = request.cpu_used();
  load.memory_used_mb = request.memory_used_mb();
  load.cpu_available = request.cpu_available();
  load.memory_available_mb = request.memory_available_mb();
  load.running_ds_count = request.running_ds_count();

  auto update_result = agent_registry.update_agent_load(request.dsa_id(), load);
  if (update_result != 0) {
    return build_rpc_error(update_result);
  }

  notify_owner_ds_exit(runtime, cleanup_result.owner_unique_id, request);
  return write_notify_ds_exit_response(rpc_context);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit