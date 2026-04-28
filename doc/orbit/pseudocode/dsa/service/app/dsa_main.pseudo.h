#pragma once

// Phase 1
// 目标: 固定 DSA 服务入口、module_impl 生命周期和 AgentService handle 注册顺序。
// 未来真实落点: src/dsa/service/app/dsa_main.cpp

namespace atorbit {

namespace shared {
namespace runtime {

class runtime_environment;

class service_shared_context {
public:
  static service_shared_context build_shared_context(runtime_environment& runtime);
  int shutdown();
};

class runtime_environment {
public:
  static runtime_environment build_runtime(int argc, const char* const argv[]);
  int app_handle();
};

}  // namespace runtime
}  // namespace shared

namespace dsa {
namespace service {
namespace app {

class dsa_main_module;

using controller_facade_t = int;
using rpc_handle_registry_t = int;
using resource_ledger_handle_t = int;
using local_channel_service_handle_t = int;
using heartbeat_monitor_handle_t = int;
using load_reporter_handle_t = int;
using controller_reporter_handle_t = int;

int add_module(int app_handle, dsa_main_module& module);
int run_app(int app_handle);
controller_facade_t create_controller_facade(shared::runtime::service_shared_context& shared_context);
rpc_handle_registry_t create_rpc_handle_registry(shared::runtime::service_shared_context& shared_context);
resource_ledger_handle_t build_resource_ledger(shared::runtime::service_shared_context& shared_context);
local_channel_service_handle_t build_local_channel_service(shared::runtime::service_shared_context& shared_context);
heartbeat_monitor_handle_t build_heartbeat_monitor(shared::runtime::service_shared_context& shared_context);
load_reporter_handle_t build_load_reporter(shared::runtime::service_shared_context& shared_context);
controller_reporter_handle_t build_controller_reporter(shared::runtime::service_shared_context& shared_context,
                                                       controller_facade_t controller_facade);
void register_agentservice_handles(rpc_handle_registry_t& registry, shared::runtime::service_shared_context& shared_context);
void register_local_channel_dispatcher(local_channel_service_handle_t& local_channel_service,
                                       shared::runtime::service_shared_context& shared_context);
void register_heartbeat_timeout_scanner(heartbeat_monitor_handle_t& heartbeat_monitor,
                                        shared::runtime::service_shared_context& shared_context);
void register_periodic_load_report(load_reporter_handle_t& load_reporter,
                                   shared::runtime::service_shared_context& shared_context);
void execute_startup_reconcile(local_channel_service_handle_t& local_channel_service,
                               heartbeat_monitor_handle_t& heartbeat_monitor,
                               controller_reporter_handle_t& controller_reporter,
                               shared::runtime::service_shared_context& shared_context);
void reload_typed_config(shared::runtime::service_shared_context& shared_context);
void refresh_region_filter(shared::runtime::service_shared_context& shared_context);
void refresh_load_report_interval(shared::runtime::service_shared_context& shared_context);
void stop_new_rpc_requests(rpc_handle_registry_t& registry);
void stop_local_dispatcher(local_channel_service_handle_t& local_channel_service);
void stop_heartbeat_scanner(heartbeat_monitor_handle_t& heartbeat_monitor);
void stop_periodic_load_report(load_reporter_handle_t& load_reporter);
void release_controller_reporter(controller_reporter_handle_t& controller_reporter);
void release_load_reporter(load_reporter_handle_t& load_reporter);
void release_heartbeat_monitor(heartbeat_monitor_handle_t& heartbeat_monitor);
void release_local_channel_service(local_channel_service_handle_t& local_channel_service);
void release_resource_ledger(resource_ledger_handle_t& resource_ledger);
void release_rpc_handle_registry(rpc_handle_registry_t& registry);
void release_controller_facade(controller_facade_t& controller_facade);

class dsa_main_module {
public:
  explicit dsa_main_module(shared::runtime::service_shared_context* shared_context);

  static int main(int argc, const char* const argv[]);

  int init();
  int reload();
  int stop();
  int cleanup();

private:
  shared::runtime::service_shared_context* shared_context_ = nullptr;
  controller_facade_t controller_facade_;
  rpc_handle_registry_t rpc_handle_registry_;
  resource_ledger_handle_t resource_ledger_;
  local_channel_service_handle_t local_channel_service_;
  heartbeat_monitor_handle_t heartbeat_monitor_;
  load_reporter_handle_t load_reporter_;
  controller_reporter_handle_t controller_reporter_;
};

}  // namespace app
}  // namespace service
}  // namespace dsa
}  // namespace atorbit
