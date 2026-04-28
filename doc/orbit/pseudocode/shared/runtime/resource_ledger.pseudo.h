#pragma once

// Phase 2
// 目标: 固化 DSA 资源账本，统一承载 reserve / commit / release / actual correction / OOM candidate 选择。
// 未来真实落点: include/atorbit/shared/runtime/resource_ledger.h

namespace atorbit {
namespace shared {
namespace runtime {

enum class ledger_result_code_t {
  k_ok = 0,
  k_invalid_argument = 1,
  k_capacity_exhausted = 2,
  k_not_found = 3,
  k_conflict = 4,
};

struct ds_resource_record {
  unsigned long long ds_id = 0;
  double expected_cpu = 0;
  double expected_memory_mb = 0;
  double actual_cpu = 0;
  double actual_memory_mb = 0;
  bool reserved_only = false;
  bool running = false;
};

struct ledger_snapshot {
  double cpu_capacity = 0;
  double memory_capacity_mb = 0;
  double cpu_used = 0;
  double memory_used_mb = 0;
  double cpu_available = 0;
  double memory_available_mb = 0;
  unsigned long long running_ds_count = 0;
  unsigned long long reserved_ds_count = 0;
};

struct oom_candidate_t {
  bool found = false;
  unsigned long long ds_id = 0;
  double actual_memory_mb = 0;
};

class resource_ledger {
public:
  resource_ledger(double cpu_capacity, double memory_capacity_mb);

  ledger_result_code_t reserve(unsigned long long ds_id, double expected_cpu, double expected_memory_mb);
  ledger_result_code_t commit_running(unsigned long long ds_id);
  ledger_result_code_t release(unsigned long long ds_id);
  ledger_result_code_t apply_actual_usage(unsigned long long ds_id, double actual_cpu, double actual_memory_mb);

  bool can_reserve(double expected_cpu, double expected_memory_mb) const;
  bool is_oversubscribed() const;
  ledger_snapshot build_snapshot() const;
  oom_candidate_t select_oom_candidate() const;

private:
  double compute_effective_cpu(const ds_resource_record& record) const;
  double compute_effective_memory_mb(const ds_resource_record& record) const;
  void recalculate_usage();
  ds_resource_record* find_record(unsigned long long ds_id);
  const ds_resource_record* find_record(unsigned long long ds_id) const;

private:
  double cpu_capacity_ = 0;
  double memory_capacity_mb_ = 0;
  double cpu_used_ = 0;
  double memory_used_mb_ = 0;
  ds_resource_record records_[64];
  unsigned long long record_count_ = 0;
};

}  // namespace runtime
}  // namespace shared
}  // namespace atorbit
