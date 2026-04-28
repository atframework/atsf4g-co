#pragma once

// Phase 1
// 目标: 固定 AgentService facade 与未来 task_action 的桥接关系。
// 未来真实落点: src/dsa/service/rpc/agent/agentservice.cpp

namespace atorbit {
namespace dsa {
namespace service {
namespace rpc {

using rpc_result_t = int;

struct rpc_context_t {};

struct ds_identity_view_t {
  unsigned long long ds_id() const;
};

struct StartDSReq {
  bool has_meta() const;
  double expected_cpu() const;
  double expected_memory_mb() const;
};

struct StopDSReq {
  bool has_ds() const;
  const ds_identity_view_t& ds() const;
};

struct QueryDSStateReq {
  unsigned long long ds_id() const;
  bool include_all() const;
};

struct DrainAgentReq {
  bool reject_new_launch() const;
  const char* reason() const;
};

struct ForwardToDSReq {};
struct AckUpstreamReq {};

struct runtime_handle_t {
  unsigned long long allocate_request_id();
  unsigned long long allocate_sequence();
};

struct task_router_t {
  int build(const char* action_name, rpc_context_t& rpc_context, const StartDSReq& request);
  int build(const char* action_name, rpc_context_t& rpc_context, const StopDSReq& request);
  int build(const char* action_name, rpc_context_t& rpc_context, const QueryDSStateReq& request);
  int build(const char* action_name, rpc_context_t& rpc_context, const DrainAgentReq& request);
  int build(const char* action_name, rpc_context_t& rpc_context, const ForwardToDSReq& request);
  int build(const char* action_name, rpc_context_t& rpc_context, const AckUpstreamReq& request);
};

task_router_t create_task_router(runtime_handle_t& runtime);
void submit_task(int task_handle);
rpc_result_t build_pending_response(rpc_context_t& rpc_context);
rpc_result_t build_accept_response(rpc_context_t& rpc_context);

class agentservice_facade {
public:
  explicit agentservice_facade(runtime_handle_t& runtime);

  rpc_result_t start_ds(rpc_context_t& rpc_context, const StartDSReq& request);
  rpc_result_t stop_ds(rpc_context_t& rpc_context, const StopDSReq& request);
  rpc_result_t query_ds_state(rpc_context_t& rpc_context, const QueryDSStateReq& request);
  rpc_result_t drain_agent(rpc_context_t& rpc_context, const DrainAgentReq& request);
  rpc_result_t forward_to_ds(rpc_context_t& rpc_context, const ForwardToDSReq& request);
  rpc_result_t ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request);

private:
  runtime_handle_t* runtime_ = nullptr;
  task_router_t task_router_;
};

}  // namespace rpc
}  // namespace service
}  // namespace dsa
}  // namespace atorbit
