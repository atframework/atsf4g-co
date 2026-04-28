#pragma once

// Phase 1
// 目标: 固定 ControllerService 在 DSC 侧的入站注册和分发入口。
// 未来真实落点: src/dsc/service/app/handle_ss_rpc_controllerservice.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace app {

using dispatcher_handle_t = int;
using runtime_handle_t = int;
struct rpc_context_t {};

using rpc_result_t = int;

class RegisterAgentReq {
public:
  bool has_agent() const {
    return has_agent_;
  }

  unsigned long long agent_id() const {
    return agent_id_;
  }

  const char* region() const {
    return region_;
  }

  double cpu_capacity() const {
    return cpu_capacity_;
  }

  double memory_capacity_mb() const {
    return memory_capacity_mb_;
  }

  double cpu_used() const {
    return cpu_used_;
  }

  double memory_used_mb() const {
    return memory_used_mb_;
  }

  unsigned long long running_ds_count() const {
    return running_ds_count_;
  }

  unsigned long long current_ds_count() const {
    return current_ds_count_;
  }

  void set_request(unsigned long long agent_id,
                   const char* region,
                   double cpu_capacity,
                   double memory_capacity_mb,
                   double cpu_used,
                   double memory_used_mb,
                   unsigned long long running_ds_count,
                   unsigned long long current_ds_count) {
    has_agent_ = true;
    agent_id_ = agent_id;
    region_ = region;
    cpu_capacity_ = cpu_capacity;
    memory_capacity_mb_ = memory_capacity_mb;
    cpu_used_ = cpu_used;
    memory_used_mb_ = memory_used_mb;
    running_ds_count_ = running_ds_count;
    current_ds_count_ = current_ds_count;
  }

private:
  bool has_agent_ = false;
  unsigned long long agent_id_ = 0;
  const char* region_ = nullptr;
  double cpu_capacity_ = 0;
  double memory_capacity_mb_ = 0;
  double cpu_used_ = 0;
  double memory_used_mb_ = 0;
  unsigned long long running_ds_count_ = 0;
  unsigned long long current_ds_count_ = 0;
};

class HeartbeatAgentReq {
public:
  bool has_agent() const {
    return has_agent_;
  }

  unsigned long long agent_id() const {
    return agent_id_;
  }

  long long now_ms() const {
    return now_ms_;
  }

  double cpu_used() const {
    return cpu_used_;
  }

  double memory_used_mb() const {
    return memory_used_mb_;
  }

  unsigned long long running_ds_count() const {
    return running_ds_count_;
  }

  void set_request(unsigned long long agent_id,
                   long long now_ms,
                   double cpu_used,
                   double memory_used_mb,
                   unsigned long long running_ds_count) {
    has_agent_ = true;
    agent_id_ = agent_id;
    now_ms_ = now_ms;
    cpu_used_ = cpu_used;
    memory_used_mb_ = memory_used_mb;
    running_ds_count_ = running_ds_count;
  }

private:
  bool has_agent_ = false;
  unsigned long long agent_id_ = 0;
  long long now_ms_ = 0;
  double cpu_used_ = 0;
  double memory_used_mb_ = 0;
  unsigned long long running_ds_count_ = 0;
};

class ReportAgentLoadReq {
public:
  bool has_agent() const {
    return has_agent_;
  }

  unsigned long long agent_id() const {
    return agent_id_;
  }

  double cpu_used() const {
    return cpu_used_;
  }

  double memory_used_mb() const {
    return memory_used_mb_;
  }

  double cpu_available() const {
    return cpu_available_;
  }

  double memory_available_mb() const {
    return memory_available_mb_;
  }

  unsigned long long running_ds_count() const {
    return running_ds_count_;
  }

  void set_request(unsigned long long agent_id,
                   double cpu_used,
                   double memory_used_mb,
                   double cpu_available,
                   double memory_available_mb,
                   unsigned long long running_ds_count) {
    has_agent_ = true;
    agent_id_ = agent_id;
    cpu_used_ = cpu_used;
    memory_used_mb_ = memory_used_mb;
    cpu_available_ = cpu_available;
    memory_available_mb_ = memory_available_mb;
    running_ds_count_ = running_ds_count;
  }

private:
  bool has_agent_ = false;
  unsigned long long agent_id_ = 0;
  double cpu_used_ = 0;
  double memory_used_mb_ = 0;
  double cpu_available_ = 0;
  double memory_available_mb_ = 0;
  unsigned long long running_ds_count_ = 0;
};
class NotifyDSStartedReq {};
class NotifyDSExitReq {
public:
  bool has_ds() const {
    return has_ds_;
  }

  unsigned long long dsa_id() const {
    return dsa_id_;
  }

  unsigned long long ds_id() const {
    return ds_id_;
  }

  int exit_reason() const {
    return exit_reason_;
  }

  int exit_code() const {
    return exit_code_;
  }

  const char* user_data() const {
    return user_data_;
  }

  double cpu_used() const {
    return cpu_used_;
  }

  double memory_used_mb() const {
    return memory_used_mb_;
  }

  double cpu_available() const {
    return cpu_available_;
  }

  double memory_available_mb() const {
    return memory_available_mb_;
  }

  unsigned long long running_ds_count() const {
    return running_ds_count_;
  }

  void set_request(unsigned long long dsa_id,
                   unsigned long long ds_id,
                   int exit_reason,
                   int exit_code,
                   const char* user_data,
                   double cpu_used,
                   double memory_used_mb,
                   double cpu_available,
                   double memory_available_mb,
                   unsigned long long running_ds_count) {
    has_ds_ = true;
    dsa_id_ = dsa_id;
    ds_id_ = ds_id;
    exit_reason_ = exit_reason;
    exit_code_ = exit_code;
    user_data_ = user_data;
    cpu_used_ = cpu_used;
    memory_used_mb_ = memory_used_mb;
    cpu_available_ = cpu_available;
    memory_available_mb_ = memory_available_mb;
    running_ds_count_ = running_ds_count;
  }

private:
  bool has_ds_ = false;
  unsigned long long dsa_id_ = 0;
  unsigned long long ds_id_ = 0;
  int exit_reason_ = 0;
  int exit_code_ = 0;
  const char* user_data_ = nullptr;
  double cpu_used_ = 0;
  double memory_used_mb_ = 0;
  double cpu_available_ = 0;
  double memory_available_mb_ = 0;
  unsigned long long running_ds_count_ = 0;
};
class ForwardFromDSReq {
public:
  bool has_ds() const {
    return has_ds_;
  }

  unsigned long long dsa_id() const {
    return dsa_id_;
  }

  unsigned long long ds_id() const {
    return ds_id_;
  }

  unsigned long long ack_seq() const {
    return ack_seq_;
  }

  const char* payload() const {
    return payload_;
  }

  void set_request(unsigned long long dsa_id,
                   unsigned long long ds_id,
                   unsigned long long ack_seq,
                   const char* payload) {
    has_ds_ = true;
    dsa_id_ = dsa_id;
    ds_id_ = ds_id;
    ack_seq_ = ack_seq;
    payload_ = payload;
  }

private:
  bool has_ds_ = false;
  unsigned long long dsa_id_ = 0;
  unsigned long long ds_id_ = 0;
  unsigned long long ack_seq_ = 0;
  const char* payload_ = nullptr;
};
class AckDownstreamReq {};
class ReconcileAgentStateReq {};

class controllerservice_facade_t {
public:
  rpc_result_t register_agent(rpc_context_t&, const RegisterAgentReq&);
  rpc_result_t heartbeat_agent(rpc_context_t&, const HeartbeatAgentReq&);
  rpc_result_t report_agent_load(rpc_context_t&, const ReportAgentLoadReq&);
  rpc_result_t notify_ds_started(rpc_context_t&, const NotifyDSStartedReq&);
  rpc_result_t notify_ds_exit(rpc_context_t&, const NotifyDSExitReq&);
  rpc_result_t forward_from_ds(rpc_context_t&, const ForwardFromDSReq&);
  rpc_result_t ack_downstream(rpc_context_t&, const AckDownstreamReq&);
  rpc_result_t reconcile_agent_state(rpc_context_t&, const ReconcileAgentStateReq&);
};

controllerservice_facade_t create_controllerservice_facade(runtime_handle_t& runtime);
rpc_result_t build_rpc_error(int error_code);
void write_invalid_request_log(rpc_context_t& rpc_context);
void bind_rpc_handler(dispatcher_handle_t& dispatcher, const char* method_name);

constexpr int ERROR_CODE_INVALID_ARGUMENT = 2;

class handle_ss_rpc_controllerservice {
public:
  int register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime);

  rpc_result_t handle_register_agent(rpc_context_t& rpc_context, const RegisterAgentReq& request);
  rpc_result_t handle_heartbeat_agent(rpc_context_t& rpc_context, const HeartbeatAgentReq& request);
  rpc_result_t handle_report_agent_load(rpc_context_t& rpc_context, const ReportAgentLoadReq& request);
  rpc_result_t handle_notify_ds_started(rpc_context_t& rpc_context, const NotifyDSStartedReq& request);
  rpc_result_t handle_notify_ds_exit(rpc_context_t& rpc_context, const NotifyDSExitReq& request);
  rpc_result_t handle_forward_from_ds(rpc_context_t& rpc_context, const ForwardFromDSReq& request);
  rpc_result_t handle_ack_downstream(rpc_context_t& rpc_context, const AckDownstreamReq& request);
  rpc_result_t handle_reconcile_agent_state(rpc_context_t& rpc_context, const ReconcileAgentStateReq& request);

private:
  rpc_result_t reject_invalid_request(rpc_context_t& rpc_context);

private:
  controllerservice_facade_t controller_facade_;
};

}  // namespace app
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
