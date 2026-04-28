#include "heartbeat_monitor.pseudo.h"

#include "../agent/controller_reporter.pseudo.h"
#include "../agent/local_channel_service.pseudo.h"

namespace atorbit {
namespace dsa {
namespace heartbeat {

heartbeat_monitor::heartbeat_monitor(shared::runtime::resource_ledger* ledger,
                                     agent::local_channel_service* local_channel_service,
                                     agent::controller_reporter* controller_reporter)
    : ledger_(ledger), local_channel_service_(local_channel_service), controller_reporter_(controller_reporter) {}

int heartbeat_monitor::track_ds(unsigned long long ds_id,
                                int process_id,
                                long long now_ms,
                                long long heartbeat_timeout_ms) {
  auto& record = records_[record_count_++];
  record.ds_id = ds_id;
  record.process_id = process_id;
  record.deadline_ms = now_ms + heartbeat_timeout_ms;
  return 0;
}

int heartbeat_monitor::refresh_heartbeat_deadline(unsigned long long ds_id,
                                                  long long now_ms,
                                                  long long heartbeat_timeout_ms,
                                                  double actual_cpu,
                                                  double actual_memory_mb) {
  auto* record = find_record(ds_id);
  if (nullptr == record) {
    return -1;
  }

  record->deadline_ms = now_ms + heartbeat_timeout_ms;
  record->actual_cpu = actual_cpu;
  record->actual_memory_mb = actual_memory_mb;
  if (nullptr != ledger_) {
    ledger_->apply_actual_usage(ds_id, actual_cpu, actual_memory_mb);
  }
  return 0;
}

int heartbeat_monitor::mark_exit_notified(unsigned long long ds_id) {
  auto* record = find_record(ds_id);
  if (nullptr == record) {
    return -1;
  }

  record->exiting = true;
  return 0;
}

int heartbeat_monitor::scan_timeout(long long now_ms) {
  for (unsigned long long index = 0; index < record_count_; ++index) {
    auto& record = records_[index];
    if (record.exiting || now_ms <= record.deadline_ms) {
      continue;
    }

    kill_process(record.ds_id, record.process_id, 3, -1);
  }

  return 0;
}

int heartbeat_monitor::kill_process(unsigned long long ds_id, int process_id, int exit_reason, int exit_code) {
  if (nullptr != controller_reporter_) {
    agent::ds_exit_report_t report;
    report.ds_id = ds_id;
    report.exit_reason = exit_reason;
    report.exit_code = exit_code;
    controller_reporter_->notify_ds_exit(report);
  }

  if (nullptr != ledger_) {
    ledger_->release(ds_id);
  }

  if (nullptr != local_channel_service_) {
    local_channel_service_->remove_ds(ds_id);
  }

  (void)process_id;
  return 0;
}

heartbeat_runtime_record_t* heartbeat_monitor::find_record(unsigned long long ds_id) {
  for (unsigned long long index = 0; index < record_count_; ++index) {
    if (records_[index].ds_id == ds_id) {
      return &records_[index];
    }
  }

  return nullptr;
}

}  // namespace heartbeat
}  // namespace dsa
}  // namespace atorbit
