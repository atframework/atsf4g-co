#pragma once

#include "../../shared/runtime/resource_ledger.pseudo.h"

// Phase 2
// 目标: 固化心跳刷新、超时扫描、强杀与退出归因流程。
// 未来真实落点: src/dsa/heartbeat/heartbeat_monitor.cpp

namespace atorbit {
namespace dsa {
namespace agent {
class local_channel_service;
class controller_reporter;
}
namespace heartbeat {

struct heartbeat_runtime_record_t {
  unsigned long long ds_id = 0;
  int process_id = 0;
  long long deadline_ms = 0;
  double actual_cpu = 0;
  double actual_memory_mb = 0;
  bool exiting = false;
};

class heartbeat_monitor {
public:
  heartbeat_monitor(shared::runtime::resource_ledger* ledger,
                    agent::local_channel_service* local_channel_service,
                    agent::controller_reporter* controller_reporter);

  int track_ds(unsigned long long ds_id, int process_id, long long now_ms, long long heartbeat_timeout_ms);
  int refresh_heartbeat_deadline(unsigned long long ds_id,
                                 long long now_ms,
                                 long long heartbeat_timeout_ms,
                                 double actual_cpu,
                                 double actual_memory_mb);
  int mark_exit_notified(unsigned long long ds_id);
  int scan_timeout(long long now_ms);

private:
  int kill_process(unsigned long long ds_id, int process_id, int exit_reason, int exit_code);
  heartbeat_runtime_record_t* find_record(unsigned long long ds_id);

private:
  shared::runtime::resource_ledger* ledger_ = nullptr;
  agent::local_channel_service* local_channel_service_ = nullptr;
  agent::controller_reporter* controller_reporter_ = nullptr;
  heartbeat_runtime_record_t records_[64];
  unsigned long long record_count_ = 0;
};

}  // namespace heartbeat
}  // namespace dsa
}  // namespace atorbit
