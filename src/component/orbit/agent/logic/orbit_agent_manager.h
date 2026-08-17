// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <uv.h>

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>

#include <atframe/atapp.h>
#include <atframe/etcdcli/etcd_keepalive.h>
#include <atframe/modules/etcd_module.h>
#include <atframe/modules/worker_pool_module.h>

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

#include <chrono>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory/rc_ptr.h"

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
  atfw::orbit::EnClientState state = atfw::orbit::EN_CLIENT_STATE_UNSPECIFIED;
  atfw::orbit::DClientLoadSnapshot load_snapshot;
  time_t last_heartbeat_timepoint = 0;
  std::string client_addr;
  uint64_t client_server_id = 0;    // 初步用于发送消息给Client
  bool notify_client_exit = false;  // 是否通知Client退出

  // 进程管理
  int64_t process_id = 0;
  uv_process_t* process_handle = nullptr;  ///< 进程句柄，由 libuv 生命周期管理，spawn 后有效

  time_t start_timepoint = 0;          ///< 启动时间点 (unix sec)
  uint64_t startup_timeout_sec = 0;    ///< STARTING 状态最大等待时间 (秒)
  uint64_t heartbeat_timeout_sec = 0;  ///< RUNNING 状态心跳最大间隔 (秒)

  time_t force_kill_timepoint = 0;
  atfw::orbit::EnClientExitReason exit_reason = atfw::orbit::EN_CLIENT_EXIT_REASON_UNSPECIFIED;
  int32_t exit_code = 0;

  // Server路由信息
  uint64_t server_unique_id = 0;
  uint64_t get_controller_server_id();
};

struct orbit_agent_server_info {
  atfw::orbit::DServerIdentity identity;
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
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_start_client(rpc::context& ctx,
                                                                    const atfw::orbit::CTAStartClientReq& request,
                                                                    atfw::orbit::ATCStartClientRsp& response);
  // 转发至 Client
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_forward_to_client(
      rpc::context& ctx, const atfw::orbit::CTAForwardToClientReq& request,
      atfw::orbit::ATCForwardToClientRsp& response);
  // Server 心跳
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_server_heartbeat(
      rpc::context& ctx, uint64_t controller_server_id, const atfw::orbit::CTAServerHeartbeatReq& request);

  // 来自Client
  // Client 启动
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_client_start(rpc::context& ctx, uint64_t client_server_id,
                                                                    const atfw::orbit::DTAClientStartReq& request,
                                                                    atfw::orbit::ATDClientStartRsp& response);
  // Client 心跳
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_client_heartbeat(
      rpc::context& ctx, const atfw::orbit::DTAClientHeartbeatNotify& request);
  // 转发至 Server
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_send_to_server(rpc::context& ctx,
                                                                      const atfw::orbit::DTASendToServerReq& request,
                                                                      atfw::orbit::ATDSendToServerRsp& response);
  // Client 退出
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_client_exit(rpc::context& ctx,
                                                                   const atfw::orbit::DTAClientExitReq& request,
                                                                   atfw::orbit::ATDClientExitRsp& response);

  void on_uv_process_exit(uv_process_t* process_handle, int64_t exit_status, int term_signal);
  uint64_t select_controller_server_id(const std::string& client_id) const;

  orbit_agent_client_record_ptr find_client(const std::string& client_id) noexcept;
  orbit_agent_client_record_ptr find_client(const std::string& client_id) const noexcept;

 private:
  int startup_seed_client();

  void set_client_state(const orbit_agent_client_record_ptr& record, atfw::orbit::EnClientState state);

  void fill_normal_client_start_command(const orbit_agent_client_record& record, uint64_t app_id,
                                        std::vector<std::string>& output) const;
  int prepare_start_client_record(const atfw::orbit::CTAStartClientReq& request, orbit_agent_client_record_ptr& output);
  int spawn_client_process(const orbit_agent_client_record_ptr& record, const std::vector<std::string>& command_line,
                           bool seed_client);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type spawn_seed_client_process(rpc::context& ctx,
                                                                          orbit_agent_client_record_ptr record);
  void build_client_launch_arguments(const orbit_agent_client_record_ptr& record,
                                     const std::unordered_map<std::string, std::string>& render_values,
                                     const std::vector<std::string>& command_line, std::vector<std::string>& output);

  void fill_client_identity(atfw::orbit::DClientIdentity& output, const orbit_agent_client_record_ptr& client) const;
  void stop_client_process(const orbit_agent_client_record_ptr& client_record,
                           atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code);
  void async_notify_client_exit(const orbit_agent_client_record_ptr& client_record);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type notify_client_exit(rpc::context& ctx,
                                                                   orbit_agent_client_record_ptr client_record,
                                                                   const std::string& custom_data);
  void check_client_timeouts(time_t now);
  void check_client_force_kill(time_t now);
  void check_server_identity_timeouts(time_t now);
  static int kill_client_process(const orbit_agent_client_record_ptr& client_record, int signal_number,
                                 atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code);

  void server_heartbeat(const atfw::orbit::DServerIdentity& server_identity);
  rpc::result_code_type agent_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                        const atfw::orbit::DServerIdentity& server_identity);
  atfw::orbit::DServerIdentity* find_server_identity(uint64_t server_unique_id);
  void on_client_process_exit(const orbit_agent_client_record_ptr& record, int64_t exit_status, int term_signal);

  void update_etcd_load_snapshot();
  void load_record_to_json();
  void try_sync_load_to_etcd();

  void delete_client(const orbit_agent_client_record_ptr& client_record);

  static double get_load_value();

  // Agent无法再提供服务
  void agent_fatal_error();

  struct spawn_completion_t {
    std::string client_id;
    uv_process_t* process_handle = nullptr;
    int64_t process_id = 0;
    int32_t uv_result = 0;
  };
  struct process_exit_action_t {
    uv_process_t* handle_ = nullptr;
    int64_t exit_status_ = 0;
    int term_signal_ = 0;
  };
  struct uv_action_t {
    bool is_spawn_completion_ = false;
    spawn_completion_t spawn_completion_;
    process_exit_action_t process_exit_action_;
  };
  void process_uv_actions();
  void process_spawn_completion(const spawn_completion_t& completion);
  void process_exit_action(const process_exit_action_t& action);
  static void worker_exit_callback(const atfw::atapp::worker_context& worker_ctx);
  static void worker_tick_callback(const atfw::atapp::worker_context& worker_ctx);
  int32_t spawn_client_async(const std::string& client_id, std::vector<std::string>&& command_line, bool detached);

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

  atfw::orbit::DAgentIdentity agent_identity_;
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

  atfw::orbit::DAgentEtcdLoadRecord load_record_;
  bool dirty_load_record_ = true;      // 负载记录是否有未同步的变更
  bool need_update_load_json_ = true;  // 是否需要更新负载记录对应的JSON字符串
  std::string load_json_;              // 上次同步到etcd的负载记录JSON字符串
  bool dirty_load_json_ = true;        // load_json_是否有未同步的变更

  uv_rusage_t last_self_rusage_{};
  std::chrono::steady_clock::time_point last_self_usage_sample_timepoint_;
  double last_self_cpu_used_ = 0.0;
  bool has_self_usage_sample_ = false;
  uint64_t sequence_allocator_ = 0;

  std::deque<server_identity_timeout_entry_t> server_identity_timeout_queue_;
  tbb::concurrent_hash_map<uint64_t, uv_loop_t*> uv_loop_queue_;
  tbb::concurrent_queue<uv_action_t> uv_actions_;
};
