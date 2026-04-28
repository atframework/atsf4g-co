#pragma once

// Phase 1
// 目标: 固定 ExternalService 在 DSC 侧的入站注册和分发入口。
// 未来真实落点: src/dsc/service/app/handle_cs_rpc_externalservice.cpp

namespace atorbit {
namespace dsc {
namespace service {
namespace app {

using dispatcher_handle_t = int;
using runtime_handle_t = int;

struct rpc_context_t {
  unsigned long long inbound_connection_handle = 0;
  const char* controller_route_key = nullptr;
  bool reconnect_resumed = false;
  unsigned long long reconnect_unique_id = 0;
  unsigned long long replay_message_count = 0;
  unsigned long long replay_seq[32] = {};
  unsigned long long replay_dsa_id[32] = {};
  unsigned long long replay_ds_id[32] = {};
  const char* replay_payload[32] = {};
  bool send_to_ds_response_written = false;
  unsigned long long send_to_ds_dsa_id = 0;
  unsigned long long send_to_ds_ds_id = 0;
  bool send_to_ds_require_ack = false;
  unsigned long long send_to_ds_ack_seq = 0;
};

using rpc_result_t = int;

class ConnectExternalReq {
public:
  bool has_session() const {
    return has_session_;
  }

  unsigned long long unique_id() const {
    return unique_id_;
  }

  void set_session(unsigned long long unique_id) {
    has_session_ = true;
    unique_id_ = unique_id;
  }

private:
  bool has_session_ = false;
  unsigned long long unique_id_ = 0;
};

class ReconnectExternalReq {
public:
  bool has_session() const {
    return has_session_;
  }

  unsigned long long unique_id() const {
    return unique_id_;
  }

  unsigned long long last_received_seq() const {
    return last_received_seq_;
  }

  void set_session(unsigned long long unique_id, unsigned long long last_received_seq) {
    has_session_ = true;
    unique_id_ = unique_id;
    last_received_seq_ = last_received_seq;
  }

private:
  bool has_session_ = false;
  unsigned long long unique_id_ = 0;
  unsigned long long last_received_seq_ = 0;
};
class LaunchDedicatedServerReq {
public:
  bool has_session() const {
    return has_session_;
  }

  unsigned long long unique_id() const {
    return unique_id_;
  }

  const char* target_region() const {
    return target_region_;
  }

  double expected_cpu() const {
    return expected_cpu_;
  }

  double expected_memory_mb() const {
    return expected_memory_mb_;
  }

  unsigned long long custom_arg_count() const {
    return custom_arg_count_;
  }

  const char* custom_arg(unsigned long long index) const {
    if (index >= custom_arg_count_) {
      return nullptr;
    }

    return custom_args_[index];
  }

  void set_request(unsigned long long unique_id,
                   const char* target_region,
                   double expected_cpu,
                   double expected_memory_mb,
                   const char* custom_args[],
                   unsigned long long custom_arg_count) {
    has_session_ = true;
    unique_id_ = unique_id;
    target_region_ = target_region;
    expected_cpu_ = expected_cpu;
    expected_memory_mb_ = expected_memory_mb;
    custom_arg_count_ = custom_arg_count < 8 ? custom_arg_count : 8;
    for (unsigned long long index = 0; index < custom_arg_count_; ++index) {
      custom_args_[index] = custom_args[index];
    }
  }

private:
  bool has_session_ = false;
  unsigned long long unique_id_ = 0;
  const char* target_region_ = nullptr;
  double expected_cpu_ = 0;
  double expected_memory_mb_ = 0;
  const char* custom_args_[8] = {};
  unsigned long long custom_arg_count_ = 0;
};
class SendToDSReq {
public:
  bool has_session() const {
    return has_session_;
  }

  unsigned long long unique_id() const {
    return unique_id_;
  }

  unsigned long long dsa_id() const {
    return dsa_id_;
  }

  unsigned long long ds_id() const {
    return ds_id_;
  }

  const char* payload() const {
    return payload_;
  }

  bool require_ack() const {
    return require_ack_;
  }

  unsigned long long ack_seq() const {
    return ack_seq_;
  }

  void set_request(unsigned long long unique_id,
                   unsigned long long dsa_id,
                   unsigned long long ds_id,
                   const char* payload,
                   bool require_ack,
                   unsigned long long ack_seq) {
    has_session_ = true;
    unique_id_ = unique_id;
    dsa_id_ = dsa_id;
    ds_id_ = ds_id;
    payload_ = payload;
    require_ack_ = require_ack;
    ack_seq_ = ack_seq;
  }

private:
  bool has_session_ = false;
  unsigned long long unique_id_ = 0;
  unsigned long long dsa_id_ = 0;
  unsigned long long ds_id_ = 0;
  const char* payload_ = nullptr;
  bool require_ack_ = false;
  unsigned long long ack_seq_ = 0;
};
class AckUpstreamReq {};
class RemoveSessionReq {};

class externalservice_facade_t {
public:
  rpc_result_t connect_external(rpc_context_t&, const ConnectExternalReq&);
  rpc_result_t reconnect_external(rpc_context_t&, const ReconnectExternalReq&);
  rpc_result_t launch_dedicated_server(rpc_context_t&, const LaunchDedicatedServerReq&);
  rpc_result_t send_to_ds(rpc_context_t&, const SendToDSReq&);
  rpc_result_t ack_upstream(rpc_context_t&, const AckUpstreamReq&);
  rpc_result_t remove_session(rpc_context_t&, const RemoveSessionReq&);
};

externalservice_facade_t create_externalservice_facade(runtime_handle_t& runtime);
rpc_result_t build_rpc_error(int error_code);
void write_invalid_request_log(rpc_context_t& rpc_context);
void bind_rpc_handler(dispatcher_handle_t& dispatcher, const char* method_name);

constexpr int ERROR_CODE_INVALID_ARGUMENT = 2;

class handle_cs_rpc_externalservice {
public:
  int register_all(dispatcher_handle_t& dispatcher, runtime_handle_t& runtime);

  rpc_result_t handle_connect_external(rpc_context_t& rpc_context, const ConnectExternalReq& request);
  rpc_result_t handle_reconnect_external(rpc_context_t& rpc_context, const ReconnectExternalReq& request);
  rpc_result_t handle_launch_dedicated_server(rpc_context_t& rpc_context, const LaunchDedicatedServerReq& request);
  rpc_result_t handle_send_to_ds(rpc_context_t& rpc_context, const SendToDSReq& request);
  rpc_result_t handle_ack_upstream(rpc_context_t& rpc_context, const AckUpstreamReq& request);
  rpc_result_t handle_remove_session(rpc_context_t& rpc_context, const RemoveSessionReq& request);

private:
  rpc_result_t reject_invalid_request(rpc_context_t& rpc_context);

private:
  externalservice_facade_t external_facade_;
};

}  // namespace app
}  // namespace service
}  // namespace dsc
}  // namespace atorbit
