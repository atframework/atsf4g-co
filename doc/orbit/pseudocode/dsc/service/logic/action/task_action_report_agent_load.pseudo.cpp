#include "task_action_report_agent_load.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

static rpc_result_t write_report_agent_load_response(rpc_context_t& rpc_context) {
  (void)rpc_context;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_report_agent_load(runtime_handle_t& runtime,
                                               rpc_context_t& rpc_context,
                                               const ReportAgentLoadReq& request,
                                               registry::agent_registry& agent_registry) {
  (void)runtime;
  if (!request.has_agent() || 0 == request.agent_id()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  registry::load_snapshot_t load;
  load.cpu_used = request.cpu_used();
  load.memory_used_mb = request.memory_used_mb();
  load.cpu_available = request.cpu_available();
  load.memory_available_mb = request.memory_available_mb();
  load.running_ds_count = request.running_ds_count();

  auto registry_result = agent_registry.update_agent_load(request.agent_id(), load);
  if (registry_result != 0) {
    return build_rpc_error(registry_result);
  }

  return write_report_agent_load_response(rpc_context);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit