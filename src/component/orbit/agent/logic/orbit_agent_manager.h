#pragma once

#include <design_pattern/singleton.h>

#include <uv.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <atframe/atapp.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/empty.pb.h>
#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/agent_service.pb.h>
#include <protocol/pbdesc/client_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_common_types.h>
#include "memory/rc_ptr.h"

#ifndef ORBIT_AGENT_SERVICE_API
#  define ORBIT_AGENT_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

namespace rpc {
class context;
}  // namespace rpc

struct orbit_agent_client_record {
  std::string client_id;  // 唯一ID 由Server传入

  // Client 启动参数
  ::google::protobuf::RepeatedPtrField<::std::string> custom_args;  // Server 传入的自定义参数
  double expected_cpu;
  double expected_memory_mb;

  // Client 状态
  int64_t process_id = 0;
  orbit::EnClientState state = orbit::EN_SLAVE_STATE_UNSPECIFIED;
  orbit::DClientLoadSnapshot load_snapshot;
  time_t last_heartbeat_timepoint = 0;
  std::string client_addr;
  uint64_t client_server_id = 0;  // 初步用于发送消息给Client

  // 进程管理
  uv_process_t* process_handle = nullptr;  ///< 进程句柄，由 libuv 生命周期管理，spawn 后有效
  time_t start_timepoint = 0;              ///< 启动时间点 (unix sec)
  uint64_t startup_timeout_sec = 0;        ///< STARTING/SEED 状态最大等待时间 (秒)
  uint64_t heartbeat_timeout_sec = 0;      ///< RUNNING 状态心跳最大间隔 (秒)

  // Server路由信息
  uint64_t server_unique_id;

  uint64_t get_controller_server_id();
};

using orbit_agent_client_record_ptr = atfw::util::memory::strong_rc_ptr<orbit_agent_client_record>;

class orbit_agent_manager : public util::design_pattern::singleton<orbit_agent_manager> {
 public:
  orbit_agent_manager();

  int init(atfw::atapp::app* app);
  void stop();
  void tick();

  // 来自Controller
  // 启动 Client
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_start_client(
      rpc::context& ctx, const orbit::CTAStartClientReq& request, orbit::ATCStartClientRsp& response);
  // 转发至 Client
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_forward_to_client(
      rpc::context& ctx, const orbit::CTAForwardToClientReq& request, orbit::ATCForwardToClientRsp& response);
  // Server 心跳
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_server_heartbeat(
      rpc::context& ctx, const orbit::CTAServerHeartbeatReq& request);

  // 来自Client
  // Client 启动
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_client_start(
      rpc::context& ctx, uint64_t client_server_id, const orbit::DTAClientStartReq& request);
  // Client 心跳
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_client_heartbeat(
      rpc::context& ctx, const orbit::DTAClientHeartbeatNotify& request);
  // 转发至 Server
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_send_to_server(
      rpc::context& ctx, const orbit::DTASendToServerNotify& request);
  // Client 退出
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_client_exit(
      rpc::context& ctx, const orbit::DTAClientExitReq& request);

  void on_client_process_exit(const std::string& client_id, int64_t exit_status, int term_signal);

 private:
  orbit::DAgentLoadSnapshot build_agent_load_snapshot() const noexcept;
  orbit_agent_client_record_ptr find_client(const std::string& client_id) noexcept;
  const orbit_agent_client_record_ptr find_client(const std::string& client_id) const noexcept;

  int prepare_start_client_record(const orbit::CTAStartClientReq& request, orbit_agent_client_record_ptr& output);
  int spawn_client_process(orbit_agent_client_record_ptr record);
  void build_client_launch_arguments(orbit_agent_client_record_ptr record, std::vector<std::string>& output) const;

  void fill_client_identity(orbit::DClientIdentity& output, orbit_agent_client_record_ptr client) const;

  void check_client_timeouts(time_t now);

  void server_heartbeat(const orbit::DServerIdentity& server_identity);
  orbit::DServerIdentity* find_server_identity(uint64_t server_unique_id);

  void update_etcd_load_snapshot();

 private:
  bool stoped_ = false;
  atfw::atapp::app* owner_app_ = nullptr;

  // 启动的Client数据
  std::unordered_map<std::string, orbit_agent_client_record_ptr> clients_;
  // Server唯一ID到ServerIdentity的映射 用于心跳和转发消息时更新路由信息
  std::unordered_map<uint64_t, orbit::DServerIdentity> server_unique_id_to_identity_;

  // 启动配置
  google::protobuf::RepeatedPtrField<std::string> tags_;
  std::vector<std::string> configured_client_command_line_;
  double cpu_capacity_ = 0.0;
  double memory_capacity_mb_ = 0.0;
  orbit::DAgentIdentity agent_identity_;

  // etcd 负载快照更新
  time_t last_load_etcd_update_timepoint_ = 0;
};