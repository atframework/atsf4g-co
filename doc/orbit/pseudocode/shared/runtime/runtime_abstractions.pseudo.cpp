#include "runtime_abstractions.pseudo.h"

namespace atorbit {
namespace shared {
namespace runtime {

runtime_environment runtime_environment::build_runtime(int argc, const char* const argv[]) {
  runtime_environment result;

  // 读取命令行和配置文件路径
  result.typed_config_ = load_typed_config(argc, argv);

  // 创建 atapp 应用对象并绑定日志、discovery、connector
  result.app_handle_ = create_app_handle(result.typed_config_);

  // 创建定时器和 ID 分配器
  result.timer_service_ = create_timer_service(result.app_handle_);
  result.request_id_allocator_ = create_request_id_allocator();
  result.sequence_allocator_ = create_sequence_allocator();
  result.error_code_bridge_ = create_error_code_bridge();

  return result;
}

uint64_t runtime_environment::allocate_request_id() {
  return request_id_allocator_.next_value();
}

uint64_t runtime_environment::allocate_sequence() {
  return sequence_allocator_.next_value();
}

int runtime_environment::map_internal_error(const char* error_name) const {
  if (nullptr == error_name || '\0' == error_name[0]) {
    return ERROR_CODE_OK;
  }

  if (is_same_error(error_name, "invalid_argument")) {
    return ERROR_CODE_INVALID_ARGUMENT;
  }

  if (is_same_error(error_name, "timeout")) {
    return ERROR_CODE_TIMEOUT;
  }

  return ERROR_CODE_INTERNAL;
}

app_handle_t& runtime_environment::app_handle() {
  return app_handle_;
}

service_shared_context service_shared_context::build_shared_context(runtime_environment& runtime) {
  service_shared_context result;

  result.runtime_ = &runtime;

  // 绑定 discovery、metrics 和停止信号
  result.discovery_handle_ = bind_discovery_handle(runtime.app_handle());
  result.metrics_handle_ = bind_metrics_handle(runtime.app_handle());
  result.stop_token_ = create_stop_token();

  return result;
}

int service_shared_context::shutdown() {
  // 标记停止，停止周期任务，并释放共享依赖
  stop_token_.mark_stopped();
  runtime_->app_handle().stop_all_periodic_tasks();
  release_discovery_handle(discovery_handle_);
  release_metrics_handle(metrics_handle_);
  return 0;
}

}  // namespace runtime
}  // namespace shared
}  // namespace atorbit
