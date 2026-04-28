#pragma once

// Phase 3.8 / 3.10
// 目标: 固化 LaunchDedicatedServer 的 inflight 预分发与 StartDS 下发。
// 未来真实落点: src/dsc/session/launch_flow.cpp

namespace atorbit {
namespace dsc {
namespace session {

using result_code_t = int;

struct launch_dispatch_request_t {
  unsigned long long owner_unique_id = 0;
  unsigned long long agent_id = 0;
  unsigned long long request_id = 0;
  double expected_cpu = 0;
  double expected_memory_mb = 0;
  const char* custom_args[8] = {};
  unsigned long long custom_arg_count = 0;
  long long now_ms = 0;
};

class start_ds_sender {
public:
  virtual ~start_ds_sender() = default;

  virtual result_code_t send_start_ds(const launch_dispatch_request_t& request) = 0;
};

class launch_flow {
public:
  explicit launch_flow(start_ds_sender* start_ds_sender_handle);

  result_code_t reserve_and_dispatch(const launch_dispatch_request_t& request);
  result_code_t complete_inflight(unsigned long long agent_id, unsigned long long request_id);
  unsigned long long prune_expired_inflight(long long now_ms);
  unsigned long long inflight_count(unsigned long long agent_id) const;

private:
  struct inflight_entry_t {
    unsigned long long owner_unique_id = 0;
    unsigned long long agent_id = 0;
    unsigned long long request_id = 0;
    long long expire_at_ms = 0;
    bool occupied = false;
  };

private:
  bool validate_request(const launch_dispatch_request_t& request) const;
  inflight_entry_t* allocate_inflight_entry(unsigned long long agent_id);
  void rollback_inflight(unsigned long long agent_id, unsigned long long request_id);

private:
  start_ds_sender* start_ds_sender_handle_ = nullptr;
  inflight_entry_t inflight_entries_[64];
  unsigned long long max_inflight_per_agent_ = 3;
  long long inflight_timeout_ms_ = 60000;
};

}  // namespace session
}  // namespace dsc
}  // namespace atorbit