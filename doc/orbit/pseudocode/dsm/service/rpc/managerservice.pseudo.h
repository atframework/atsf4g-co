#pragma once

#include "../app/handle_ss_rpc_managerservice.pseudo.h"

// Phase 5.1 / 5.2
// 目标: 固定 ManagerService facade 与未来 task_action 的桥接关系。
// 未来真实落点: src/dsm/service/rpc/manager/managerservice.cpp

namespace atorbit {
namespace dsm {
namespace service {
namespace rpc {

struct task_router_t {
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::RegisterControllerReq& request);
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::ReportControllerSnapshotReq& request);
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::QueryInventoryReq& request);
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::StopDedicatedServerReq& request);
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::DrainControllerReq& request);
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::DrainRegionReq& request);
  int build(const char* action_name, app::rpc_context_t& rpc_context, const app::ApplyRoutingPlanReq& request);
};

task_router_t create_task_router(app::runtime_handle_t& runtime);
void submit_task(int task_handle);
app::rpc_result_t build_accept_response(app::rpc_context_t& rpc_context);

class managerservice_facade {
public:
  explicit managerservice_facade(app::runtime_handle_t& runtime);

  app::rpc_result_t register_controller(app::rpc_context_t& rpc_context, const app::RegisterControllerReq& request);
  app::rpc_result_t report_controller_snapshot(app::rpc_context_t& rpc_context,
                                               const app::ReportControllerSnapshotReq& request);
  app::rpc_result_t query_inventory(app::rpc_context_t& rpc_context, const app::QueryInventoryReq& request);
  app::rpc_result_t stop_dedicated_server(app::rpc_context_t& rpc_context, const app::StopDedicatedServerReq& request);
  app::rpc_result_t drain_controller(app::rpc_context_t& rpc_context, const app::DrainControllerReq& request);
  app::rpc_result_t drain_region(app::rpc_context_t& rpc_context, const app::DrainRegionReq& request);
  app::rpc_result_t apply_routing_plan(app::rpc_context_t& rpc_context, const app::ApplyRoutingPlanReq& request);

private:
  app::runtime_handle_t* runtime_ = nullptr;
  task_router_t task_router_;
};

}  // namespace rpc
}  // namespace service
}  // namespace dsm
}  // namespace atorbit