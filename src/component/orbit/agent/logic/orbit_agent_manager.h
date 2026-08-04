#pragma once

#include <design_pattern/singleton.h>

#include <uv.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <atframe/atapp.h>
#include <atframe/etcdcli/etcd_keepalive.h>
#include <atframe/modules/etcd_module.h>

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
  std::string client_id;      // 唯一ID 由Server传入
  bool seed_process = false;  // 是否为种子进程

  // Client 启动参数
  ::google::protobuf::RepeatedPtrField<::std::string> custom_args;  // Server 传入的自定义参数
  double expected_cpu = 0.0f;
  double expected_memory_mb = 0.0f;

  // Client 状态
  orbit::EnClientState state = orbit::EN_CLIENT_STATE_UNSPECIFIED;
  orbit::DClientLoadSnapshot load_snapshot;
  time_t last_heartbeat_timepoint = 0;
  std::string client_addr;
  uint64_t client_server_id = 0;  // 初步用于发送消息给Client

  // 进程管理
  int64_t process_id = 0;
  uv_process_t* process_handle = nullptr;  ///< 进程句柄，由 libuv 生命周期管理，spawn 后有效

  time_t start_timepoint = 0;          ///< 启动时间点 (unix sec)
  uint64_t startup_timeout_sec = 0;    ///< STARTING 状态最大等待时间 (秒)
  uint64_t heartbeat_timeout_sec = 0;  ///< RUNNING 状态心跳最大间隔 (秒)

  time_t force_kill_timepoint = 0;
  orbit::EnClientExitReason force_exit_reason = orbit::EN_CLIENT_EXIT_REASON_UNSPECIFIED;
  int32_t force_exit_code = 0;

  // Server路由信息
  uint64_t server_unique_id = 0;
  uint64_t get_controller_server_id();
};

struct orbit_agent_server_info {
  orbit::DServerIdentity identity;
  time_t expire_timepoint = 0;
};

using orbit_agent_client_record_ptr = atfw::util::memory::strong_rc_ptr<orbit_agent_client_record>;

class orbit_agent_manager : public util::design_pattern::singleton<orbit_agent_manager> {
 public:
  orbit_agent_manager();

  int init(atfw::atapp::app* app);
  int stop();
  void cleanup();
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
      rpc::context& ctx, uint64_t controller_server_id, const orbit::CTAServerHeartbeatReq& request);

  // 来自Client
  // Client 启动
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_client_start(
      rpc::context& ctx, uint64_t client_server_id, const orbit::DTAClientStartReq& request,
      orbit::ATDClientStartRsp& response);
  // Client 心跳
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_client_heartbeat(
      rpc::context& ctx, const orbit::DTAClientHeartbeatNotify& request);
  // 转发至 Server
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_send_to_server(
      rpc::context& ctx, const orbit::DTASendToServerReq& request, orbit::ATDSendToServerRsp& response);
  // Client 退出
  EXPLICIT_NODISCARD_ATTR ORBIT_AGENT_SERVICE_API rpc::result_code_type handle_client_exit(
      rpc::context& ctx, const orbit::DTAClientExitReq& request, orbit::ATDClientExitRsp& response);

  void on_client_process_exit(const std::string& client_id, int64_t exit_status, int term_signal);
  uint64_t select_controller_server_id(const std::string& client_id) const;

 private:
  int startup_seed_client();

  orbit_agent_client_record_ptr find_client(const std::string& client_id) noexcept;
  const orbit_agent_client_record_ptr find_client(const std::string& client_id) const noexcept;
  void set_client_state(orbit_agent_client_record_ptr record, orbit::EnClientState state);

  void fill_normal_client_start_command(const orbit_agent_client_record& record, uint64_t app_id,
                                        std::vector<std::string>& output) const;
  int prepare_start_client_record(const orbit::CTAStartClientReq& request, orbit_agent_client_record_ptr& output);
  int spawn_client_process(orbit_agent_client_record_ptr record, const std::vector<std::string>& command_line);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type spawn_seed_client_process(rpc::context& ctx,
                                                                          orbit_agent_client_record_ptr record);
  void build_client_launch_arguments(orbit_agent_client_record_ptr record,
                                     const std::unordered_map<std::string, std::string>& render_values,
                                     const std::vector<std::string>& command_line, std::vector<std::string>& output);

  void fill_client_identity(orbit::DClientIdentity& output, orbit_agent_client_record_ptr client) const;
  void stop_client_process(orbit_agent_client_record_ptr client_record);
  void check_client_timeouts(time_t now);
  void check_client_force_kill(time_t now);
  void check_server_identity_timeouts(time_t now);
  int kill_client_process(orbit_agent_client_record_ptr client_record, int signal_number,
                          orbit::EnClientExitReason force_exit_reason, int32_t force_exit_code);

  void server_heartbeat(const orbit::DServerIdentity& server_identity);
  rpc::result_code_type agent_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                        const orbit::DServerIdentity& server_identity);
  orbit::DServerIdentity* find_server_identity(uint64_t server_unique_id);

  void update_etcd_load_snapshot();
  void load_record_to_json();
  void try_sync_load_to_etcd();

  void delete_client(orbit_agent_client_record_ptr client_record);

  double get_load_value();

  // Agent无法再提供服务
  void agent_fatal_error();

  void on_client_start_success();
  void on_client_start_failure();

 private:
  bool stoped_ = false;
  atfw::atapp::app* owner_app_ = nullptr;

  // 启动的Client数据
  std::unordered_map<std::string, orbit_agent_client_record_ptr> clients_;
  std::unordered_map<uint64_t, std::set<std::string>> server_unique_id_to_client_ids_;
  orbit_agent_client_record_ptr seed_client_record_;  // 种子进程记录
  int32_t batch_startup_count_ = 0;                   // 当前批次启动的Client数量
  bool agent_online_ = false;                         // Agent是否可以开始分配
  int32_t repeated_startup_failures_ = 0;             // Client 连续启动失败

  // Server唯一ID到ServerIdentity的映射 用于心跳和转发消息时更新路由信息
  std::unordered_map<uint64_t, orbit_agent_server_info> server_unique_id_to_identity_;

  // 启动配置
  std::string region_;
  std::string agent_endpoint_;
  std::string tag_;
  std::vector<std::string> configured_client_command_line_;
  double cpu_capacity_ = 0.0;
  double memory_capacity_mb_ = 0.0;
  time_t server_identity_timeout_sec_ = 0;
  time_t server_identity_check_interval_sec_ = 0;
  int32_t max_batch_startup_count_ = 0;
  bool seed_mode_enabled_ = false;
  uint32_t seed_startup_timeout_sec_ = 0;
  uint32_t seed_heartbeat_timeout_sec_ = 0;
  std::vector<std::string> seed_client_command_line_;

  orbit::DAgentIdentity agent_identity_;
  atfw::atapp::protocol::atapp_metadata controller_policy_selector_;

  struct server_identity_timeout_entry_t {
    uint64_t server_unique_id = 0;
    time_t expire_timepoint = 0;
  };

  // 负载快照更新
  time_t last_auto_load_etcd_update_timepoint_ = 0;
  time_t last_server_identity_timeout_check_timepoint_ = 0;
  atapp::etcd_module etcd_mod_;
  atapp::etcd_keepalive::ptr_t keepalive_actor_ = nullptr;

  orbit::DAgentEtcdLoadRecord load_record_;
  bool dirty_load_record_ = true;      // 负载记录是否有未同步的变更
  bool need_update_load_json_ = true;  // 是否需要更新负载记录对应的JSON字符串
  std::string load_json_;              // 上次同步到etcd的负载记录JSON字符串
  bool dirty_load_json_ = true;        // load_json_是否有未同步的变更

  uv_rusage_t last_self_rusage_;
  std::chrono::steady_clock::time_point last_self_usage_sample_timepoint_;
  double last_self_cpu_used_ = 0.0;
  bool has_self_usage_sample_ = false;
  uint64_t sequence_allocator_ = 0;

  std::deque<server_identity_timeout_entry_t> server_identity_timeout_queue_;
};