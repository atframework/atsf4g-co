#pragma once

// Phase 5.1 / 5.2
// 目标: 固定 ManagerService 在 DSM 侧的入站注册和分发入口。
// 未来真实落点: src/dsm/service/app/handle_ss_rpc_managerservice.cpp

namespace atorbit {
namespace dsm {
namespace service {
namespace app {

using dispatcher_handle_t = int;
using runtime_handle_t = int;
using rpc_result_t = int;

constexpr unsigned long long REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT = 64;
constexpr unsigned long long QUERY_INVENTORY_CONTROLLER_LIMIT = 32;
constexpr unsigned long long QUERY_INVENTORY_DS_LIMIT = 64;

struct rpc_context_t {
  bool register_controller_response_written = false;
  unsigned long long registered_controller_id = 0;
  const char* registered_region = nullptr;
  bool report_controller_snapshot_response_written = false;
  unsigned long long reported_controller_id = 0;
  unsigned long long reported_ds_count = 0;
  bool reported_draining = false;
  bool query_inventory_response_written = false;
  const char* queried_region = nullptr;
  unsigned long long queried_controller_count = 0;
  unsigned long long queried_agent_count = 0;
  unsigned long long queried_ds_count = 0;
  unsigned long long queried_session_count = 0;
  unsigned long long queried_inflight_count = 0;
  unsigned long long queried_draining_controller_count = 0;
  unsigned long long queried_detail_count = 0;
  bool queried_detail_truncated = false;
  unsigned long long queried_controller_id[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  const char* queried_controller_region[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  const char* queried_controller_addr[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  unsigned long long queried_controller_agent_count[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  unsigned long long queried_controller_ds_count[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  unsigned long long queried_controller_session_count[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  unsigned long long queried_controller_inflight_count[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  bool queried_controller_draining[QUERY_INVENTORY_CONTROLLER_LIMIT] = {};
  unsigned long long queried_ds_detail_count = 0;
  bool queried_ds_detail_truncated = false;
  unsigned long long queried_ds_controller_id[QUERY_INVENTORY_DS_LIMIT] = {};
  const char* queried_ds_region[QUERY_INVENTORY_DS_LIMIT] = {};
  unsigned long long queried_ds_dsa_id[QUERY_INVENTORY_DS_LIMIT] = {};
  unsigned long long queried_ds_id[QUERY_INVENTORY_DS_LIMIT] = {};
  unsigned long long queried_ds_owner_unique_id[QUERY_INVENTORY_DS_LIMIT] = {};
  int queried_ds_running_state[QUERY_INVENTORY_DS_LIMIT] = {};
  int queried_ds_exit_reason[QUERY_INVENTORY_DS_LIMIT] = {};
  unsigned long long queried_ds_last_active_millis[QUERY_INVENTORY_DS_LIMIT] = {};
};

class RegisterControllerReq {
public:
  bool has_controller() const {
    return has_controller_;
  }

  unsigned long long controller_id() const {
    return controller_id_;
  }

  const char* region() const {
    return region_;
  }

  const char* controller_addr() const {
    return controller_addr_;
  }

  void set_request(unsigned long long controller_id, const char* region, const char* controller_addr) {
    has_controller_ = true;
    controller_id_ = controller_id;
    region_ = region;
    controller_addr_ = controller_addr;
  }

private:
  bool has_controller_ = false;
  unsigned long long controller_id_ = 0;
  const char* region_ = nullptr;
  const char* controller_addr_ = nullptr;
};

class ReportControllerSnapshotReq {
public:
  bool has_snapshot() const {
    return has_snapshot_;
  }

  unsigned long long controller_id() const {
    return controller_id_;
  }

  unsigned long long agent_count() const {
    return agent_count_;
  }

  unsigned long long ds_count() const {
    return ds_count_;
  }

  unsigned long long session_count() const {
    return session_count_;
  }

  unsigned long long inflight_count() const {
    return inflight_count_;
  }

  bool draining() const {
    return draining_;
  }

  unsigned long long ds_detail_count() const {
    return ds_detail_count_;
  }

  unsigned long long ds_detail_dsa_id(unsigned long long index) const {
    return ds_detail_dsa_id_[index];
  }

  unsigned long long ds_detail_ds_id(unsigned long long index) const {
    return ds_detail_ds_id_[index];
  }

  unsigned long long ds_detail_owner_unique_id(unsigned long long index) const {
    return ds_detail_owner_unique_id_[index];
  }

  int ds_detail_running_state(unsigned long long index) const {
    return ds_detail_running_state_[index];
  }

  int ds_detail_exit_reason(unsigned long long index) const {
    return ds_detail_exit_reason_[index];
  }

  unsigned long long ds_detail_last_active_millis(unsigned long long index) const {
    return ds_detail_last_active_millis_[index];
  }

  void set_request(unsigned long long controller_id,
                   unsigned long long agent_count,
                   unsigned long long ds_count,
                   unsigned long long session_count,
                   unsigned long long inflight_count,
                   bool draining) {
    has_snapshot_ = true;
    controller_id_ = controller_id;
    agent_count_ = agent_count;
    ds_count_ = ds_count;
    session_count_ = session_count;
    inflight_count_ = inflight_count;
    draining_ = draining;
  }

  bool append_ds_detail(unsigned long long dsa_id,
                        unsigned long long ds_id,
                        unsigned long long owner_unique_id,
                        int running_state,
                        int exit_reason,
                        unsigned long long last_active_millis) {
    if (ds_detail_count_ >= REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT) {
      return false;
    }

    ds_detail_dsa_id_[ds_detail_count_] = dsa_id;
    ds_detail_ds_id_[ds_detail_count_] = ds_id;
    ds_detail_owner_unique_id_[ds_detail_count_] = owner_unique_id;
    ds_detail_running_state_[ds_detail_count_] = running_state;
    ds_detail_exit_reason_[ds_detail_count_] = exit_reason;
    ds_detail_last_active_millis_[ds_detail_count_] = last_active_millis;
    ++ds_detail_count_;
    return true;
  }

private:
  bool has_snapshot_ = false;
  unsigned long long controller_id_ = 0;
  unsigned long long agent_count_ = 0;
  unsigned long long ds_count_ = 0;
  unsigned long long session_count_ = 0;
  unsigned long long inflight_count_ = 0;
  bool draining_ = false;
  unsigned long long ds_detail_count_ = 0;
  unsigned long long ds_detail_dsa_id_[REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
  unsigned long long ds_detail_ds_id_[REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
  unsigned long long ds_detail_owner_unique_id_[REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
  int ds_detail_running_state_[REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
  int ds_detail_exit_reason_[REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
  unsigned long long ds_detail_last_active_millis_[REPORT_CONTROLLER_SNAPSHOT_DS_LIMIT] = {};
};
class QueryInventoryReq {
public:
  bool has_region_filter() const {
    return has_region_filter_;
  }

  const char* region() const {
    return region_;
  }

  bool has_controller_filter() const {
    return has_controller_filter_;
  }

  unsigned long long controller_id() const {
    return controller_id_;
  }

  void set_region_filter(const char* region) {
    has_region_filter_ = nullptr != region;
    region_ = region;
  }

  void set_controller_filter(unsigned long long controller_id) {
    has_controller_filter_ = 0 != controller_id;
    controller_id_ = controller_id;
  }

  void set_request(unsigned long long controller_id, const char* region) {
    set_controller_filter(controller_id);
    set_region_filter(region);
  }

private:
  bool has_region_filter_ = false;
  const char* region_ = nullptr;
  bool has_controller_filter_ = false;
  unsigned long long controller_id_ = 0;
};
class StopDedicatedServerReq {};
class DrainControllerReq {};
class DrainRegionReq {};
class ApplyRoutingPlanReq {};

class managerservice_facade_t {
public:
  rpc_result_t register_controller(rpc_context_t&, const RegisterControllerReq&);
  rpc_result_t report_controller_snapshot(rpc_context_t&, const ReportControllerSnapshotReq&);
  rpc_result_t query_inventory(rpc_context_t&, const QueryInventoryReq&);
  rpc_result_t stop_dedicated_server(rpc_context_t&, const StopDedicatedServerReq&);
  rpc_result_t drain_controller(rpc_context_t&, const DrainControllerReq&);
  rpc_result_t drain_region(rpc_context_t&, const DrainRegionReq&);
  rpc_result_t apply_routing_plan(rpc_context_t&, const ApplyRoutingPlanReq&);
};

managerservice_facade_t create_managerservice_facade(runtime_handle_t& runtime);
rpc_result_t build_rpc_error(int error_code);
void write_invalid_request_log(rpc_context_t& rpc_context);
void bind_rpc_handler(dispatcher_handle_t& dispatcher, const char* method_name);

constexpr int ERROR_CODE_INVALID_ARGUMENT = 2;

class handle_ss_rpc_managerservice {
public:
  int register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime);

  rpc_result_t handle_register_controller(rpc_context_t& rpc_context, const RegisterControllerReq& request);
  rpc_result_t handle_report_controller_snapshot(rpc_context_t& rpc_context, const ReportControllerSnapshotReq& request);
  rpc_result_t handle_query_inventory(rpc_context_t& rpc_context, const QueryInventoryReq& request);
  rpc_result_t handle_stop_dedicated_server(rpc_context_t& rpc_context, const StopDedicatedServerReq& request);
  rpc_result_t handle_drain_controller(rpc_context_t& rpc_context, const DrainControllerReq& request);
  rpc_result_t handle_drain_region(rpc_context_t& rpc_context, const DrainRegionReq& request);
  rpc_result_t handle_apply_routing_plan(rpc_context_t& rpc_context, const ApplyRoutingPlanReq& request);

private:
  rpc_result_t reject_invalid_request(rpc_context_t& rpc_context);

private:
  managerservice_facade_t manager_facade_;
};

}  // namespace app
}  // namespace service
}  // namespace dsm
}  // namespace atorbit