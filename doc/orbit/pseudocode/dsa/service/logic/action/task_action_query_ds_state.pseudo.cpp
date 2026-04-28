#include "task_action_query_ds_state.pseudo.h"

namespace atorbit {
namespace dsa {
namespace service {
namespace logic {
namespace action {

rpc_result_t run_task_action_query_ds_state(runtime_handle_t& runtime,
                                            rpc_context_t& rpc_context,
                                            const QueryDSStateReq& request) {
  auto& ledger = get_resource_ledger(runtime);
  auto snapshot = ledger.build_snapshot();

  if (request.include_all()) {
    // 枚举所有运行中与 reserved DS，并拼装 DSRuntimeSnapshot 列表。
    return write_query_ds_state_response(rpc_context, snapshot, enumerate_ds_runtime_snapshot(runtime));
  }

  return write_query_ds_state_response(rpc_context, snapshot, enumerate_single_ds_snapshot(runtime, request.ds_id()));
}

rpc_result_t run_task_action_drain_agent(runtime_handle_t& runtime,
                                         rpc_context_t& rpc_context,
                                         const DrainAgentReq& request) {
  set_agent_draining(runtime, request.reject_new_launch());
  record_drain_reason(runtime, request.reason());
  return write_ok_response(rpc_context);
}

int start_dsa_startup_reconcile(shared::runtime::service_shared_context& shared_context,
                                controller_reporter_handle_t& controller_reporter,
                                load_reporter_handle_t& load_reporter,
                                resource_ledger_handle_t& resource_ledger) {
  auto inventory = scan_existing_ds_processes(shared_context);

  // 重建本地 channel、资源账本和运行中 DS 清单，然后向 DSC 执行全量 reconcile。
  rebuild_local_runtime_from_inventory(inventory, resource_ledger);
  auto snapshot = build_reconcile_snapshot(load_reporter);
  return controller_reporter.reconcile_agent_state(extract_agent_id(shared_context), snapshot);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsa
}  // namespace atorbit
