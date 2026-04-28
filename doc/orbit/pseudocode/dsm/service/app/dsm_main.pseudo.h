#pragma once

// Phase 5.1
// 目标: 固定 DSM 服务入口、module_impl 生命周期和 ManagerService handle 注册顺序。
// 未来真实落点: src/dsm/service/app/dsm_main.cpp

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

namespace dsm {
namespace service {
namespace app {

class dsm_main_module;

using manager_facade_t = int;
using rpc_handle_registry_t = int;
using cluster_catalog_handle_t = int;

int add_module(int app_handle, dsm_main_module& module);
int run_app(int app_handle);
manager_facade_t create_managerservice_facade(shared::runtime::service_shared_context& shared_context);
rpc_handle_registry_t create_rpc_handle_registry(shared::runtime::service_shared_context& shared_context);
cluster_catalog_handle_t build_cluster_catalog(shared::runtime::service_shared_context& shared_context);
void register_managerservice_handles(rpc_handle_registry_t& registry, shared::runtime::service_shared_context& shared_context);
void reload_typed_config(shared::runtime::service_shared_context& shared_context);
void refresh_control_policy(shared::runtime::service_shared_context& shared_context);
void stop_new_rpc_requests(rpc_handle_registry_t& registry);
void stop_background_control_jobs(shared::runtime::service_shared_context& shared_context);
void release_cluster_catalog(shared::runtime::service_shared_context& shared_context);
void release_rpc_handle_registry(rpc_handle_registry_t& registry);

class dsm_main_module {
public:
  explicit dsm_main_module(shared::runtime::service_shared_context* shared_context);

  static int main(int argc, const char* const argv[]);

  int init();
  int reload();
  int stop();
  int cleanup();

private:
  shared::runtime::service_shared_context* shared_context_ = nullptr;
  manager_facade_t manager_facade_;
  rpc_handle_registry_t rpc_handle_registry_;
  cluster_catalog_handle_t cluster_catalog_;
};

}  // namespace app
}  // namespace service
}  // namespace dsm
}  // namespace atorbit