#include "dsc_main.pseudo.h"

namespace atorbit {
namespace dsc {
namespace service {
namespace app {

dsc_main_module::dsc_main_module(shared::runtime::service_shared_context* shared_context)
    : shared_context_(shared_context) {}

int dsc_main_module::main(int argc, const char* const argv[]) {
  auto runtime = shared::runtime::runtime_environment::build_runtime(argc, argv);
  auto shared_context = shared::runtime::service_shared_context::build_shared_context(runtime);
  auto app = runtime.app_handle();

  // 创建主模块并注册到 atapp
  auto module = dsc_main_module(&shared_context);
  add_module(app, module);

  // 启动应用主循环
  return run_app(app);
}

int dsc_main_module::init() {
  // 创建 facade 和 RPC 注册表
  controller_facade_ = create_controllerservice_facade(*shared_context_);
  external_facade_ = create_externalservice_facade(*shared_context_);
  rpc_handle_registry_ = create_rpc_handle_registry(*shared_context_);

  // 初始化 registry、session router，并注册双向 RPC handle
  build_agent_registry(*shared_context_);
  build_session_router(*shared_context_);
  register_controllerservice_handles(rpc_handle_registry_, *shared_context_);
  register_externalservice_handles(rpc_handle_registry_, *shared_context_);
  return 0;
}

int dsc_main_module::reload() {
  // 重新读取调度和会话配置
  reload_typed_config(*shared_context_);
  refresh_scheduler_policy(*shared_context_);
  refresh_session_policy(*shared_context_);
  return 0;
}

int dsc_main_module::stop() {
  // 停止新 DSA / 外部服务接入和后台清理任务
  stop_new_rpc_requests(rpc_handle_registry_);
  stop_background_cleanup(*shared_context_);
  return 0;
}

int dsc_main_module::cleanup() {
  // 释放 registry、router 和 facade
  release_agent_registry(*shared_context_);
  release_session_router(*shared_context_);
  release_rpc_handle_registry(rpc_handle_registry_);
  return shared_context_->shutdown();
}

}  // namespace app
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
