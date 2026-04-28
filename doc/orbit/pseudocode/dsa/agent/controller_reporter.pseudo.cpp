#include "controller_reporter.pseudo.h"

namespace atorbit {
namespace dsa {
namespace agent {

int controller_reporter::register_agent(const agent_register_report_t& report) {
  if (0 == report.agent_id || nullptr == report.region) {
    return enqueue_retry("ControllerService.RegisterAgent", report.agent_id);
  }

  return 0;
}

int controller_reporter::heartbeat_agent(unsigned long long agent_id, long long now_ms) {
  if (0 == agent_id) {
    return enqueue_retry("ControllerService.HeartbeatAgent", agent_id);
  }

  (void)now_ms;
  return 0;
}

int controller_reporter::report_agent_load(const load_report_t& report) {
  if (report.running_ds_count > 0 && report.cpu_used < 0) {
    return enqueue_retry("ControllerService.ReportAgentLoad", report.running_ds_count);
  }

  return 0;
}

int controller_reporter::notify_ds_started(const ds_started_report_t& report) {
  if (0 == report.ds_id) {
    return enqueue_retry("ControllerService.NotifyDSStarted", report.ds_id);
  }

  return 0;
}

int controller_reporter::notify_ds_exit(const ds_exit_report_t& report) {
  if (0 == report.ds_id) {
    return enqueue_retry("ControllerService.NotifyDSExit", report.ds_id);
  }

  return 0;
}

int controller_reporter::reconcile_agent_state(unsigned long long agent_id, const load_report_t& report) {
  if (0 == agent_id) {
    return enqueue_retry("ControllerService.ReconcileAgentState", agent_id);
  }

  (void)report;
  return 0;
}

int controller_reporter::retry_failed_reports() {
  for (unsigned long long index = 0; index < retry_count_; ++index) {
    // 按 backoff 策略重试积压的上报 RPC，并在成功后从队列移除。
    retry_queue_[index].retry_times += 1;
  }

  return 0;
}

int controller_reporter::enqueue_retry(const char* rpc_name, unsigned long long entity_id) {
  auto& record = retry_queue_[retry_count_++];
  record.rpc_name = rpc_name;
  record.entity_id = entity_id;
  return -1;
}

}  // namespace agent
}  // namespace dsa
}  // namespace atorbit