#pragma once

// Phase 1
// 目标: 固定 DSA / DSC 共用的运行时抽象、配置入口、定时器和 ID 分配器。
// 未来真实落点: include/atorbit/shared/runtime/* + src/shared/runtime/*

namespace atorbit {
namespace shared {
namespace runtime {

class runtime_environment {
public:
  static runtime_environment build_runtime(int argc, const char* const argv[]);

  uint64_t allocate_request_id();
  uint64_t allocate_sequence();
  int map_internal_error(const char* error_name) const;

  app_handle_t& app_handle();

private:
  app_handle_t app_handle_;
  typed_config_t typed_config_;
  timer_service_t timer_service_;
  id_allocator_t request_id_allocator_;
  id_allocator_t sequence_allocator_;
  error_code_bridge_t error_code_bridge_;
};

class service_shared_context {
public:
  static service_shared_context build_shared_context(runtime_environment& runtime);

  int shutdown();

private:
  runtime_environment* runtime_ = nullptr;
  discovery_handle_t discovery_handle_;
  metrics_handle_t metrics_handle_;
  stop_token_t stop_token_;
};

}  // namespace runtime
}  // namespace shared
}  // namespace atorbit
