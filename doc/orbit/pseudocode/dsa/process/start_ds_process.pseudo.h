#pragma once

#include "../../../shared/runtime/resource_ledger.pseudo.h"

// Phase 2
// 目标: 固化 DSA 拉起 DS 的子进程与本地 channel 建立流程。
// 未来真实落点: src/dsa/process/start_ds_process.cpp

namespace atorbit {
namespace dsa {
namespace agent {
class local_channel_service;
}
namespace process {

enum class start_ds_status_t {
  k_ok = 0,
  k_invalid_argument = 1,
  k_launch_failed = 2,
  k_channel_failed = 3,
};

struct start_ds_request_t {
  unsigned long long request_id = 0;
  unsigned long long ds_id = 0;
  double expected_cpu = 0;
  double expected_memory_mb = 0;
  const char* custom_args[8] = {};
  unsigned long long custom_arg_count = 0;
};

struct start_ds_result_t {
  start_ds_status_t status = start_ds_status_t::k_ok;
  int process_id = 0;
  const char* local_endpoint = nullptr;
};

class start_ds_process {
public:
  start_ds_process(shared::runtime::resource_ledger* ledger, agent::local_channel_service* local_channel_service);

  start_ds_result_t launch(const start_ds_request_t& request);
  void rollback_failed_launch(unsigned long long ds_id);

private:
  int start_process(const start_ds_request_t& request, const char* const launch_args[], unsigned long long launch_arg_count);
  const char* create_local_endpoint(unsigned long long ds_id);
  unsigned long long build_launch_args(const start_ds_request_t& request, const char* output_args[], unsigned long long capacity) const;

private:
  shared::runtime::resource_ledger* ledger_ = nullptr;
  agent::local_channel_service* local_channel_service_ = nullptr;
};

}  // namespace process
}  // namespace dsa
}  // namespace atorbit
