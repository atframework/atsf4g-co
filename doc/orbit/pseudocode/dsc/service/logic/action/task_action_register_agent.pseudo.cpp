#include "task_action_register_agent.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

static rpc_result_t write_register_agent_response(rpc_context_t& rpc_context) {
  (void)rpc_context;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_register_agent(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const RegisterAgentReq& request,
                                            registry::agent_registry& agent_registry) {
  (void)runtime;
  if (!request.has_agent() || 0 == request.agent_id() || nullptr == request.region()) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  registry::agent_registration_t registration;
  registration.agent_id = request.agent_id();
  registration.region = request.region();
  registration.load.cpu_capacity = request.cpu_capacity();
  registration.load.memory_capacity_mb = request.memory_capacity_mb();
  registration.load.cpu_used = request.cpu_used();
  registration.load.memory_used_mb = request.memory_used_mb();
  registration.load.running_ds_count = request.running_ds_count();
  registration.load.current_ds_count = request.current_ds_count();

  auto registry_result = agent_registry.upsert_registered_agent(registration);
  if (registry_result != 0) {
    return build_rpc_error(registry_result);
  }

  return write_register_agent_response(rpc_context);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit