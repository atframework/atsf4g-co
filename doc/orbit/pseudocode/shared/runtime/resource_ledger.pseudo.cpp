#include "resource_ledger.pseudo.h"

namespace atorbit {
namespace shared {
namespace runtime {

namespace {

static double max_double(double left, double right) {
  return left >= right ? left : right;
}

}  // namespace

resource_ledger::resource_ledger(double cpu_capacity, double memory_capacity_mb)
    : cpu_capacity_(cpu_capacity), memory_capacity_mb_(memory_capacity_mb) {}

ledger_result_code_t resource_ledger::reserve(unsigned long long ds_id, double expected_cpu, double expected_memory_mb) {
  if (0 == ds_id || expected_cpu <= 0 || expected_memory_mb <= 0) {
    return ledger_result_code_t::k_invalid_argument;
  }

  if (nullptr != find_record(ds_id)) {
    return ledger_result_code_t::k_conflict;
  }

  if (!can_reserve(expected_cpu, expected_memory_mb)) {
    return ledger_result_code_t::k_capacity_exhausted;
  }

  auto& record = records_[record_count_++];
  record.ds_id = ds_id;
  record.expected_cpu = expected_cpu;
  record.expected_memory_mb = expected_memory_mb;
  record.reserved_only = true;
  record.running = false;
  recalculate_usage();
  return ledger_result_code_t::k_ok;
}

ledger_result_code_t resource_ledger::commit_running(unsigned long long ds_id) {
  auto* record = find_record(ds_id);
  if (nullptr == record) {
    return ledger_result_code_t::k_not_found;
  }

  record->reserved_only = false;
  record->running = true;
  recalculate_usage();
  return ledger_result_code_t::k_ok;
}

ledger_result_code_t resource_ledger::release(unsigned long long ds_id) {
  for (unsigned long long index = 0; index < record_count_; ++index) {
    if (records_[index].ds_id != ds_id) {
      continue;
    }

    records_[index] = records_[record_count_ - 1];
    --record_count_;
    recalculate_usage();
    return ledger_result_code_t::k_ok;
  }

  return ledger_result_code_t::k_not_found;
}

ledger_result_code_t resource_ledger::apply_actual_usage(unsigned long long ds_id, double actual_cpu, double actual_memory_mb) {
  auto* record = find_record(ds_id);
  if (nullptr == record) {
    return ledger_result_code_t::k_not_found;
  }

  record->actual_cpu = actual_cpu;
  record->actual_memory_mb = actual_memory_mb;
  recalculate_usage();
  return ledger_result_code_t::k_ok;
}

bool resource_ledger::can_reserve(double expected_cpu, double expected_memory_mb) const {
  return expected_cpu <= build_snapshot().cpu_available && expected_memory_mb <= build_snapshot().memory_available_mb;
}

bool resource_ledger::is_oversubscribed() const {
  return cpu_used_ > cpu_capacity_ || memory_used_mb_ > memory_capacity_mb_;
}

ledger_snapshot resource_ledger::build_snapshot() const {
  ledger_snapshot snapshot;
  snapshot.cpu_capacity = cpu_capacity_;
  snapshot.memory_capacity_mb = memory_capacity_mb_;
  snapshot.cpu_used = cpu_used_;
  snapshot.memory_used_mb = memory_used_mb_;
  snapshot.cpu_available = cpu_capacity_ - cpu_used_;
  snapshot.memory_available_mb = memory_capacity_mb_ - memory_used_mb_;

  for (unsigned long long index = 0; index < record_count_; ++index) {
    if (records_[index].running) {
      ++snapshot.running_ds_count;
    }
    if (records_[index].reserved_only) {
      ++snapshot.reserved_ds_count;
    }
  }

  return snapshot;
}

oom_candidate_t resource_ledger::select_oom_candidate() const {
  oom_candidate_t candidate;

  for (unsigned long long index = 0; index < record_count_; ++index) {
    const auto& record = records_[index];
    if (!record.running) {
      continue;
    }

    if (!candidate.found || record.actual_memory_mb > candidate.actual_memory_mb) {
      candidate.found = true;
      candidate.ds_id = record.ds_id;
      candidate.actual_memory_mb = record.actual_memory_mb;
    }
  }

  return candidate;
}

double resource_ledger::compute_effective_cpu(const ds_resource_record& record) const {
  return max_double(record.expected_cpu, record.actual_cpu);
}

double resource_ledger::compute_effective_memory_mb(const ds_resource_record& record) const {
  return max_double(record.expected_memory_mb, record.actual_memory_mb);
}

void resource_ledger::recalculate_usage() {
  cpu_used_ = 0;
  memory_used_mb_ = 0;

  for (unsigned long long index = 0; index < record_count_; ++index) {
    cpu_used_ += compute_effective_cpu(records_[index]);
    memory_used_mb_ += compute_effective_memory_mb(records_[index]);
  }
}

ds_resource_record* resource_ledger::find_record(unsigned long long ds_id) {
  for (unsigned long long index = 0; index < record_count_; ++index) {
    if (records_[index].ds_id == ds_id) {
      return &records_[index];
    }
  }

  return nullptr;
}

const ds_resource_record* resource_ledger::find_record(unsigned long long ds_id) const {
  for (unsigned long long index = 0; index < record_count_; ++index) {
    if (records_[index].ds_id == ds_id) {
      return &records_[index];
    }
  }

  return nullptr;
}

}  // namespace runtime
}  // namespace shared
}  // namespace atorbit
