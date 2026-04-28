#include "task_action_launch_dedicated_server.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_NO_CANDIDATE = 21;
constexpr int ERROR_CODE_LAUNCH_DISPATCH_FAILED = 22;

static unsigned long long allocate_request_id(runtime_handle_t& runtime) {
  (void)runtime;
  static unsigned long long next_request_id = 4000;
  return ++next_request_id;
}

static long long get_now_ms(runtime_handle_t& runtime) {
  (void)runtime;
  return 1000;
}

static rpc_result_t write_launch_dedicated_server_response(rpc_context_t& rpc_context, unsigned long long request_id) {
  (void)rpc_context;
  (void)request_id;
  return 0;
}

}  // namespace

rpc_result_t run_task_action_launch_dedicated_server(runtime_handle_t& runtime,
                                                     rpc_context_t& rpc_context,
                                                     const LaunchDedicatedServerReq& request,
                                                     scheduler::scheduler_service& scheduler_service,
                                                     session::launch_flow& launch_flow) {
  if (!request.has_session() || 0 == request.unique_id() || nullptr == request.target_region() ||
      request.expected_cpu() <= 0 || request.expected_memory_mb() <= 0) {
    write_invalid_request_log(rpc_context);
    return build_rpc_error(ERROR_CODE_INVALID_ARGUMENT);
  }

  scheduler::launch_request_t scheduler_request;
  scheduler_request.owner_unique_id = request.unique_id();
  scheduler_request.target_region = request.target_region();
  scheduler_request.expected_cpu = request.expected_cpu();
  scheduler_request.expected_memory_mb = request.expected_memory_mb();

  auto selected = scheduler_service.select_agent_for_launch(scheduler_request);
  if (!selected.found) {
    return build_rpc_error(ERROR_CODE_NO_CANDIDATE);
  }

  session::launch_dispatch_request_t dispatch_request;
  dispatch_request.owner_unique_id = request.unique_id();
  dispatch_request.agent_id = selected.agent_id;
  dispatch_request.request_id = allocate_request_id(runtime);
  dispatch_request.expected_cpu = request.expected_cpu();
  dispatch_request.expected_memory_mb = request.expected_memory_mb();
  dispatch_request.custom_arg_count = request.custom_arg_count();
  dispatch_request.now_ms = get_now_ms(runtime);
  for (unsigned long long index = 0; index < dispatch_request.custom_arg_count; ++index) {
    dispatch_request.custom_args[index] = request.custom_arg(index);
  }

  auto launch_result = launch_flow.reserve_and_dispatch(dispatch_request);
  if (launch_result != 0) {
    return build_rpc_error(ERROR_CODE_LAUNCH_DISPATCH_FAILED);
  }

  return write_launch_dedicated_server_response(rpc_context, dispatch_request.request_id);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsc
}  // namespace atorbit