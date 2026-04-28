#include "dsa_main.pseudo.h"

namespace atorbit {
namespace dsa {
namespace service {
namespace app {

dsa_main_module::dsa_main_module(shared::runtime::service_shared_context* shared_context)
    : shared_context_(shared_context) {}

int dsa_main_module::main(int argc, const char* const argv[]) {
  auto runtime = shared::runtime::runtime_environment::build_runtime(argc, argv);
  auto shared_context = shared::runtime::service_shared_context::build_shared_context(runtime);
  auto app = runtime.app_handle();

  // 创建主模块并注册到 atapp
  auto module = dsa_main_module(&shared_context);
  add_module(app, module);

  // 启动应用主循环
  return run_app(app);
}

int dsa_main_module::init() {
  if (nullptr == shared_context_) {
    return -1;
  }

  controller_facade_ = create_controller_facade(*shared_context_);
  rpc_handle_registry_ = create_rpc_handle_registry(*shared_context_);
  resource_ledger_ = build_resource_ledger(*shared_context_);
  local_channel_service_ = build_local_channel_service(*shared_context_);
  heartbeat_monitor_ = build_heartbeat_monitor(*shared_context_);
  load_reporter_ = build_load_reporter(*shared_context_);
  controller_reporter_ = build_controller_reporter(*shared_context_, controller_facade_);

  register_agentservice_handles(rpc_handle_registry_, *shared_context_);
  register_local_channel_dispatcher(local_channel_service_, *shared_context_);
  register_heartbeat_timeout_scanner(heartbeat_monitor_, *shared_context_);
  register_periodic_load_report(load_reporter_, *shared_context_);
  execute_startup_reconcile(local_channel_service_, heartbeat_monitor_, controller_reporter_, *shared_context_);
  return 0;
}

int dsa_main_module::reload() {
  if (nullptr == shared_context_) {
    return -1;
  }

  reload_typed_config(*shared_context_);
  refresh_region_filter(*shared_context_);
  refresh_load_report_interval(*shared_context_);
  return 0;
}

int dsa_main_module::stop() {
  stop_new_rpc_requests(rpc_handle_registry_);
  stop_periodic_load_report(load_reporter_);
  stop_heartbeat_scanner(heartbeat_monitor_);
  stop_local_dispatcher(local_channel_service_);
  return 0;
}

int dsa_main_module::cleanup() {
  if (nullptr == shared_context_) {
    return -1;
  }

  release_controller_reporter(controller_reporter_);
  release_load_reporter(load_reporter_);
  release_heartbeat_monitor(heartbeat_monitor_);
  release_local_channel_service(local_channel_service_);
  release_resource_ledger(resource_ledger_);
  release_rpc_handle_registry(rpc_handle_registry_);
  release_controller_facade(controller_facade_);
  return shared_context_->shutdown();
}

}  // namespace app
}  // namespace service
}  // namespace dsa
}  // namespace atorbit
