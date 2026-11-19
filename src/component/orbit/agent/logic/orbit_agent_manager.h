// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <uv.h>

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_set.h>

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
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "memory/rc_ptr.h"

namespace rpc {
class context;
}  // namespace rpc

struct orbit_agent_client_record {
  // Agent 拉起进程时分配的通信标识
  uint64_t client_instance_id = 0;
  std::string client_id;
  // 拉起时使用的模板
  int32_t client_template_id = 0;

  bool seed_process = false;             // 是否为种子进程
  bool seed_ready = false;               // 种子进程是否已 Ready，可作为 Fork 源
  bool seed_startup_resolved = false;    // 种子进程的启动结果是否已记录，避免重复结算
  bool pre_start = false;                // 是否曾经为预拉起
  bool claimed = false;                  // 是否已认领 非预启动默认认领
  bool start_client_sent = false;        // 是否发送启动请求
  bool startup_failure_counted = false;  // 是否已经记录启动失败

  // Client 启动参数
  ::google::protobuf::RepeatedPtrField<::std::string> custom_args;  // 来自模板的启动参数
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
  ///< 进程句柄，由 libuv 生命周期管理，spawn 后有效 对于他的操作都需要在worker线程上
  uv_process_t* process_handle_main_thread = nullptr;
  bool remote_start = false;

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

ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_BEGIN
template <class CharT>
struct ATFW_UTIL_SYMBOL_VISIBLE formatter<orbit_agent_client_record, CharT>
    : formatter<basic_string_view<CharT>, CharT> {
  template <class FormatContext>
  auto format(const orbit_agent_client_record& record, FormatContext& ctx) const {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "client {}({})", record.client_id, record.client_instance_id);
  }
};

template <class CharT>
struct ATFW_UTIL_SYMBOL_VISIBLE formatter<orbit_agent_client_record_ptr, CharT>
    : formatter<basic_string_view<CharT>, CharT> {
  template <class FormatContext>
  auto format(const orbit_agent_client_record_ptr& record, FormatContext& ctx) const {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "client {}({})", record->client_id, record->client_instance_id);
  }
};
ATFRAMEWORK_UTILS_STRING_FWAPI_NAMESPACE_END

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
  int32_t handle_server_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                                  const atfw::orbit::CTAServerHeartbeatReq& request);

  // 来自Client
  // Client 启动
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_client_start(rpc::context& ctx, uint64_t client_server_id,
                                                                    const atfw::orbit::DTAClientStartReq& request,
                                                                    atfw::orbit::ATDClientStartRsp& response);
  // Client 心跳
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type handle_client_heartbeat(
      rpc::context& ctx, const atfw::orbit::DTAClientHeartbeatReq& request,
      atfw::orbit::ATDClientHeartbeatRsp& response);
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

  // 按通信标识查找
  orbit_agent_client_record_ptr find_client(uint64_t client_instance_id) noexcept;
  orbit_agent_client_record_ptr find_client(uint64_t client_instance_id) const noexcept;
  // 按真实 client_id 查找（仅已认领的 Client 能查到）
  orbit_agent_client_record_ptr find_client_by_client_id(const std::string& client_id) noexcept;
  orbit_agent_client_record_ptr find_client_by_client_id(const std::string& client_id) const noexcept;

  const std::string& get_agent_instance_id() const noexcept { return agent_instance_id_; }

  void update_etcd_load_snapshot();
  const atfw::orbit::DAgentEtcdLoadRecord& get_load_record() const noexcept { return load_record_; }

 private:
  // ---- 种子进程 ----
  // 遍历 Excel，为本 Agent tag 匹配且开启 seed_mode 的模板各拉起一个种子进程
  int startup_seed_client();
  // 本机拉起种子进程：不走 RPC，供 init 阶段调用
  int spawn_seed_client_process(const orbit_agent_client_record_ptr& record);
  // 按模板查找可作为 Fork 源的种子进程
  orbit_agent_client_record_ptr find_seed_client(int32_t client_template_id) noexcept;
  // 记录种子进程启动结果；所有种子都出结果后把 Agent 置为可对外服务
  void resolve_seed_client_startup(const orbit_agent_client_record_ptr& record, bool success);

  // ---- 预启动 ----
  // 从 Excel 读出的客户端模板参数
  struct client_template_t {
    int32_t client_template_id = 0;
    double expected_cpu = 0.0;
    double expected_memory_mb = 0.0;
    uint32_t startup_timeout_sec = 0;
    uint32_t heartbeat_timeout_sec = 0;
    uint32_t pre_start_count = 0;
    bool remote_start = false;
    bool seed_mode = false;
    std::string match_tag;
    std::vector<std::string> launch_args;
  };

  // 读 Excel 配置，缓存本 Agent tag 匹配的模板；没有任何匹配模板时返回错误
  int init_pre_start_templates();
  // 按 client_template_id 读表
  bool load_client_template(int32_t client_template_id, client_template_t& output) const;
  // tick 内按表补齐预启动进程
  void tick_pre_start(time_t now);
  // 连续启动失败后停掉某个模板的预启动
  void disable_pre_start_template(int32_t client_template_id);
  // 找出该模板下已 ready 且未被认领的预启动进程
  orbit_agent_client_record_ptr find_idle_pre_start_client(int32_t client_template_id) noexcept;
  // 把预启动进程绑定到本次启动请求
  void claime_client(const orbit_agent_client_record_ptr& client_record, const atfw::orbit::CTAStartClientReq& request);
  // 异步下发 start_client 并通知 Controller（预启动认领）
  void schedule_start_claimed_client(const orbit_agent_client_record_ptr& client_record);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type start_claimed_client(rpc::context& ctx,
                                                                     orbit_agent_client_record_ptr client_record);
  // 异步下发 start_client（普通启动：Client 上报 Ready 后被接受时下发）
  void schedule_send_start_client(const orbit_agent_client_record_ptr& client_record);
  // 向 Client 下发 start_client
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_start_client(rpc::context& ctx,
                                                                  orbit_agent_client_record_ptr client_record);
  // 未下发 start_client 就退出时，按"启动失败"累加计数
  void record_client_startup_failure(const orbit_agent_client_record_ptr& client_record);
  // 启动成功
  void record_client_startup_success(const orbit_agent_client_record_ptr& client_record);
  // 周期打印 Agent 运行情况（各模板的预启动/运行数量）
  void log_agent_running_summary(time_t now);

  void set_client_state(const orbit_agent_client_record_ptr& record, atfw::orbit::EnClientState state);

  void fill_render_values(const orbit_agent_client_record_ptr& record,
                          std::unordered_map<std::string, std::string>& render_values) const;
  void fill_normal_client_start_command(const orbit_agent_client_record& record, uint64_t app_id,
                                        std::vector<std::string>& output, bool remote_start) const;
  int prepare_start_client_record(const atfw::orbit::CTAStartClientReq& request,
                                  const client_template_t& client_template, orbit_agent_client_record_ptr& output);
  // 拉起 Client：Seed 模式下从同模板种子进程 Fork，否则本机 spawn
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type spawn_client_process(rpc::context& ctx,
                                                                     orbit_agent_client_record_ptr record);
  // 异步调度 spawn_client_process，失败时清理记录
  void schedule_spawn_client_process(const orbit_agent_client_record_ptr& client_record);
  // 从种子进程 Fork 出子进程
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type fork_seed_client_process(rpc::context& ctx,
                                                                         orbit_agent_client_record_ptr record,
                                                                         orbit_agent_client_record_ptr seed_record);
  rpc::result_code_type remote_spawn_client_process(rpc::context& ctx, const orbit_agent_client_record_ptr& record,
                                                    const std::vector<std::string>& command_line);
  void build_client_launch_arguments(const orbit_agent_client_record_ptr& record,
                                     const std::unordered_map<std::string, std::string>& render_values,
                                     const std::string& client_path, const std::vector<std::string>& command_line,
                                     std::vector<std::string>& output, bool remote_start);

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
  int kill_client_process(const orbit_agent_client_record_ptr& client_record, int signal_number,
                          atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code);

  void server_heartbeat(const atfw::orbit::DServerIdentity& server_identity);
  int32_t agent_heartbeat(rpc::context& ctx, uint64_t controller_server_id,
                          const atfw::orbit::DServerIdentity& server_identity);
  const atfw::orbit::DServerIdentity* find_server_identity(uint64_t server_unique_id) const;
  void on_client_process_exit(const orbit_agent_client_record_ptr& record, int64_t exit_status, int term_signal);

  void load_record_to_json();
  void try_sync_load_to_etcd();

  void delete_client(const orbit_agent_client_record_ptr& client_record);

  double get_load_value();
  double get_load_limit_value();
  double calculate_load_limit();

  // Agent无法再提供服务
  void agent_fatal_error();

  struct spawn_completion_t {
    uint64_t client_instance_id = 0;
    uv_process_t* process_handle = nullptr;
    int64_t process_id = 0;
    int32_t uv_result = 0;
  };
  struct process_exit_action_t {
    uint64_t client_instance_id = 0;
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
  static void delete_uv_process_handle(uv_process_t* process_handle);
  static void worker_exit_callback(const atfw::atapp::worker_context& worker_ctx);
  static void worker_tick_callback(const atfw::atapp::worker_context& worker_ctx);
  int32_t spawn_client_async(uint64_t client_instance_id, std::vector<std::string>&& command_line, bool detached);

 private:
  bool stoped_ = false;
  atfw::atapp::app* owner_app_ = nullptr;

  // 启动的Client数据，key 为通信标识
  std::unordered_map<uint64_t, orbit_agent_client_record_ptr> clients_;
  // 种子进程 以 template_id 为key；就绪后作为该模板的 Fork 源，进程退出时移除
  std::unordered_map<int32_t, orbit_agent_client_record_ptr> seed_clients_;
  // 尚未出启动结果的种子进程数量：全部出结果（成功或失败）后 Agent 才置为可分配
  int32_t seed_startup_pending_count_ = 0;
  int32_t seed_startup_success_count_ = 0;
  int32_t seed_startup_failure_count_ = 0;
  // 已认领的真实 client_id → 通信标识
  std::unordered_map<std::string, uint64_t> client_id_to_instance_id_;
  std::unordered_map<uint64_t, std::set<std::string>> server_unique_id_to_client_ids_;
  int32_t batch_startup_count_ = 0;        // 当前批次启动的Client数量
  bool agent_online_ = false;              // Agent是否可以开始分配
  int32_t repeated_startup_failures_ = 0;  // Client 连续启动失败

  // Server唯一ID到ServerIdentity的映射 用于心跳和转发消息时更新路由信息
  std::unordered_map<uint64_t, orbit_agent_server_info> server_unique_id_to_identity_;

  // 启动配置
  std::string region_;
  std::string agent_endpoint_;
  std::string remote_agent_endpoint_;
  std::string tag_;
  std::string client_ip_;
  std::string log_name_;
  double cpu_capacity_ = 0.0;
  double memory_capacity_mb_ = 0.0;
  time_t server_identity_timeout_sec_ = 0;
  time_t server_identity_check_interval_sec_ = 0;
  int32_t max_batch_startup_count_ = 0;
  bool seed_mode_enabled_ = false;
  int32_t repeated_startup_failures_fatal_error_ = 0;
  std::atomic<double> load_usage_ = 0.0;
  std::atomic<double> load_limit_ = 0.0;
  bool stop_running_client_ = true;

  // Agent 实例唯一标识，随启动参数下发给 Client
  std::string agent_instance_id_;
  // 预启动
  bool enable_pre_start_ = false;
  std::vector<int32_t> pre_start_template_ids_;  // 本 Agent tag 匹配且需要预启动的模板
  time_t last_summary_log_timepoint_ = 0;        // 上次打印运行情况的时间点
  // 未认领的预启动进程连续启动失败次数，按 client_template_id 统计
  std::unordered_map<int32_t, uint32_t> pre_start_repeated_failures_;
  // 连续失败到该次数后停止该模板的预启动，0 表示不限制
  uint32_t pre_start_repeated_failures_limit_ = 0;

  std::string client_path_;
  std::vector<std::string> client_command_line_;

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
  struct uv_loop_data {
    uv_loop_t* loop_ = nullptr;
    std::unordered_set<uv_process_t*> process_handles_;
  };
  tbb::concurrent_hash_map<uint64_t, std::shared_ptr<uv_loop_data>> uv_loop_queue_;
  tbb::concurrent_hash_map<uv_process_t*, int> need_kill_process_;
  tbb::concurrent_queue<uv_action_t> uv_actions_;
};
