#pragma once

// Phase 1
// 目标: 固定 DSC 服务入口、module_impl 生命周期和双向 RPC handle 注册顺序。
// 未来真实落点: src/dsc/service/app/dsc_main.cpp

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

namespace dsc {
namespace service {
namespace app {

class dsc_main_module;

using controller_facade_t = int;
using external_facade_t = int;
using rpc_handle_registry_t = int;

int add_module(int app_handle, dsc_main_module& module);
int run_app(int app_handle);
controller_facade_t create_controllerservice_facade(shared::runtime::service_shared_context& shared_context);
external_facade_t create_externalservice_facade(shared::runtime::service_shared_context& shared_context);
rpc_handle_registry_t create_rpc_handle_registry(shared::runtime::service_shared_context& shared_context);
void build_agent_registry(shared::runtime::service_shared_context& shared_context);
void build_session_router(shared::runtime::service_shared_context& shared_context);
void register_controllerservice_handles(rpc_handle_registry_t& registry, shared::runtime::service_shared_context& shared_context);
void register_externalservice_handles(rpc_handle_registry_t& registry, shared::runtime::service_shared_context& shared_context);
void reload_typed_config(shared::runtime::service_shared_context& shared_context);
void refresh_scheduler_policy(shared::runtime::service_shared_context& shared_context);
void refresh_session_policy(shared::runtime::service_shared_context& shared_context);
void stop_new_rpc_requests(rpc_handle_registry_t& registry);
void stop_background_cleanup(shared::runtime::service_shared_context& shared_context);
void release_agent_registry(shared::runtime::service_shared_context& shared_context);
void release_session_router(shared::runtime::service_shared_context& shared_context);
void release_rpc_handle_registry(rpc_handle_registry_t& registry);

class dsc_main_module {
public:
  explicit dsc_main_module(shared::runtime::service_shared_context* shared_context);

  static int main(int argc, const char* const argv[]);

  int init();
  int reload();
  int stop();
  int cleanup();

private:
  shared::runtime::service_shared_context* shared_context_ = nullptr;
  controller_facade_t controller_facade_;
  external_facade_t external_facade_;
  rpc_handle_registry_t rpc_handle_registry_;
};

}  // namespace app
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
