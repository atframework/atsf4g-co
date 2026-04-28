#include "dsm_main.pseudo.h"

namespace atorbit {
namespace dsm {
namespace service {
namespace app {

dsm_main_module::dsm_main_module(shared::runtime::service_shared_context* shared_context)
    : shared_context_(shared_context) {}

int dsm_main_module::main(int argc, const char* const argv[]) {
  auto runtime = shared::runtime::runtime_environment::build_runtime(argc, argv);
  auto shared_context = shared::runtime::service_shared_context::build_shared_context(runtime);
  auto app = runtime.app_handle();

  auto module = dsm_main_module(&shared_context);
  add_module(app, module);
  return run_app(app);
}

int dsm_main_module::init() {
  manager_facade_ = create_managerservice_facade(*shared_context_);
  rpc_handle_registry_ = create_rpc_handle_registry(*shared_context_);
  cluster_catalog_ = build_cluster_catalog(*shared_context_);

  register_managerservice_handles(rpc_handle_registry_, *shared_context_);
  return 0;
}

int dsm_main_module::reload() {
  reload_typed_config(*shared_context_);
  refresh_control_policy(*shared_context_);
  return 0;
}

int dsm_main_module::stop() {
  stop_new_rpc_requests(rpc_handle_registry_);
  stop_background_control_jobs(*shared_context_);
  return 0;
}

int dsm_main_module::cleanup() {
  release_cluster_catalog(*shared_context_);
  release_rpc_handle_registry(rpc_handle_registry_);
  return shared_context_->shutdown();
}

}  // namespace app
}  // namespace service
}  // namespace dsm
}  // namespace atorbit