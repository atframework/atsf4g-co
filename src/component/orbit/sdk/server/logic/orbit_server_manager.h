// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <atframe/atapp.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframe/atapp_conf.pb.h>
#include <google/protobuf/empty.pb.h>
#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/server_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/rc_ptr.h>
#include <rpc/rpc_common_types.h>

#ifndef ORBIT_SERVER_SERVICE_API
#  define ORBIT_SERVER_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

enum class EnClientStatus : int {
  EN_CLIENT_STATUS_UNSPECIFIED = 0,
  EN_CLIENT_STATUS_STARTING = 1,
  EN_CLIENT_STATUS_RUNNING = 2,
  EN_CLIENT_STATUS_EXITED = 3,
};

struct client_info {
  std::string client_id;
  EnClientStatus status;
  std::string region;

  orbit::DClientIdentity client_identity;
};

namespace rpc {
class context;
}  // namespace rpc

using client_info_ptr = atfw::util::memory::strong_rc_ptr<client_info>;

using on_forward_to_server_fn =
    std::function<rpc::result_code_type(rpc::context& ctx, std::string client_id, const std::string& data)>;
using on_client_start_notify_fn = std::function<rpc::result_code_type(
    rpc::context& ctx, std::string client_id, const std::string& client_addr, const std::string& data)>;
using on_client_end_notify_fn =
    std::function<rpc::result_code_type(rpc::context& ctx, std::string client_id, orbit::EnClientExitReason exit_reason,
                                        const std::string& exit_data, int32_t exit_code)>;

class orbit_server_manager : public util::design_pattern::singleton<orbit_server_manager> {
 public:
  int init(uint64_t unique_id, uint64_t heartbeat_interval_sec);
  void stop();
  void tick();

 public:
  // 启动Client
  EXPLICIT_NODISCARD_ATTR ORBIT_SERVER_SERVICE_API rpc::result_code_type start_client(
      rpc::context& ctx, const std::string& region, const orbit::DAgentClientStartArgs& args,
      const google::protobuf::RepeatedPtrField<std::string>& match_tags);
  // 发送消息至Client
  EXPLICIT_NODISCARD_ATTR ORBIT_SERVER_SERVICE_API rpc::result_code_type send_to_client(rpc::context& ctx,
                                                                                        const std::string& client_id,
                                                                                        const std::string& data);

  void set_on_forward_to_server(on_forward_to_server_fn fn) { on_forward_to_server_ = std::move(fn); }
  void set_on_client_start_notify(on_client_start_notify_fn fn) { on_client_start_notify_ = std::move(fn); }
  void set_on_client_end_notify(on_client_end_notify_fn fn) { on_client_end_notify_ = std::move(fn); }

 public:
  // SDK 内部使用
  // 收到Client消息
  ORBIT_SERVER_SERVICE_API rpc::result_code_type handle_forward_to_server(rpc::context& ctx,
                                                                          const orbit::CTSForwardToServerNotify& req);
  // 收到Client Start通知
  ORBIT_SERVER_SERVICE_API rpc::result_code_type handle_client_start_notify(rpc::context& ctx,
                                                                            const orbit::CTSClientStartNotify& req);
  // 收到Client End通知
  ORBIT_SERVER_SERVICE_API rpc::result_code_type handle_client_end_notify(rpc::context& ctx,
                                                                          const orbit::CTSClientEndNotify& req);

 private:
  void server_heartbeat();
  client_info_ptr get_client_info(std::string client_id);
  uint64_t select_controller_server_id(const std::string& client_id, const std::string& region);
  uint64_t select_controller_server_id(const std::string& region);

  uint64_t heartbeat_interval_sec_ = 0;
  time_t last_heartbeat_time_ = 0;

  on_forward_to_server_fn on_forward_to_server_;
  on_client_start_notify_fn on_client_start_notify_;
  on_client_end_notify_fn on_client_end_notify_;

  orbit::DServerIdentity server_identity_;

  std::unordered_map<std::string, client_info_ptr> client_info_map_;
  std::unordered_map<std::string, std::unordered_set<std::string>> client_region_map_;
};