// Copyright 2026 atframework

#include "logic/orbit_server_manager.h"

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <config/logic_config.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <opentelemetry/semconv/incubating/rpc_attributes.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/rpc_utils.h>
#include <rpc/servertocontrollerservice/servertocontrollerservice.atfw.gen.h>

#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "handle/handle_ss_rpc_controllertoserverservice.atfw.gen.h"

#if defined(ORBIT_SERVER_SDK_DLL) && ORBIT_SERVER_SDK_DLL
#  if defined(ORBIT_SERVER_SDK_NATIVE) && ORBIT_SERVER_SDK_NATIVE
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(orbit_server_manager);
#  else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(orbit_server_manager);
#  endif
#else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(orbit_server_manager);
#endif

ORBIT_SERVER_SERVICE_API orbit_server_manager::orbit_server_manager() {}
ORBIT_SERVER_SERVICE_API orbit_server_manager::~orbit_server_manager() {}

ORBIT_SERVER_SERVICE_API int orbit_server_manager::init(uint64_t unique_id, uint64_t heartbeat_interval_sec) {
  server_identity_.set_unique_id(unique_id);
  server_identity_.set_server_node_id(logic_config::me()->get_local_server_id());
  heartbeat_interval_sec_ = heartbeat_interval_sec;
  if (heartbeat_interval_sec_ <= 0) {
    heartbeat_interval_sec_ = 5;
  }
  client_timeout_sec_ = 60;  // 保底时间

  INIT_CALL_FN(handle::controllertoserverservice::register_handles_for_controllertoserverservice);
  return 0;
}

ORBIT_SERVER_SERVICE_API void orbit_server_manager::stop() {}

ORBIT_SERVER_SERVICE_API void orbit_server_manager::tick() {
  server_heartbeat();
  check_client_timeout();
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_server_manager::start_client(
    rpc::context& ctx, const std::string& region, const atfw::orbit::DAgentClientStartArgs& args) {
  const std::string& client_id = args.client_start_args().client_id().client_id();
  if (client_id.empty()) {
    FWLOGERROR("orbit start_client rejected: client_id is empty");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ != nullptr) {
    FWLOGERROR("already found client info for client identity {}", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_ALREADY_EXISTS);
  }

  uint64_t controller_server_id = select_controller_server_id(client_id, region);
  if (controller_server_id == 0) {
    FWLOGERROR("failed to select controller server for region {}", region);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CONTROLLER_SERVER_NOT_FOUND);
  }

  client_info_ptr_ = atfw::component::memory::stl::make_strong_rc<client_info>();
  client_info_ptr_->client_id = client_id;
  client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_START_CONFIRMING;
  client_info_ptr_->region = region;
  client_info_map_[client_id] = client_info_ptr_;
  client_region_map_[region].insert(client_id);

  auto req = rpc::make_shared_message<atfw::orbit::STCLaunchClientReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::CTSLaunchClientRsp>(ctx);

  *req->mutable_server_identity() = server_identity_;
  *req->mutable_args() = args;

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::servertocontrollerservice::launch_client(ctx, controller_server_id, *req, *rsp));
  if (rpc_result == 0) {
    rpc_result = rsp->error_code();
  }
  if (rpc_result != 0) {
    FWLOGERROR("orbit launch_client failed for {} to controller {:#x}, res: {}", client_id, controller_server_id,
               rpc_result);
    client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_EXITED;
    erase_client_info(client_id);
    RPC_RETURN_CODE(rpc_result);
  }
  if (client_info_ptr_->status == EnClientStatus::EN_CLIENT_STATUS_START_CONFIRMING) {
    client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_STARTING;
  }
  client_info_ptr_->client_identity = rsp->client_identity();
  if (args.startup_timeout_sec() > 0) {
    add_client_timeout(client_info_ptr_, args.startup_timeout_sec() + 10);  // 容忍时间
  } else {
    add_client_timeout(client_info_ptr_);  // 保底超时
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_SERVICE_API int32_t orbit_server_manager::send_to_client_no_wait(rpc::context& ctx,
                                                                              const std::string& client_id,
                                                                              const void* msg_data, size_t msg_size) {
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    FWLOGERROR("failed to find client info for client identity {}", client_id);
    return PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_FOUND;
  }

  if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_RUNNING) {
    FWLOGERROR("client {} is not in running status, current status: {}", client_id,
               static_cast<int>(client_info_ptr_->status));
    return PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_RUNNING;
  }

  uint64_t controller_server_id = select_controller_server_id(client_id, client_info_ptr_->region);
  if (controller_server_id == 0) {
    FWLOGERROR("failed to select controller server for region {}", client_info_ptr_->region);
    return PROJECT_NAMESPACE_ID::err::EN_SERVER_CONTROLLER_SERVER_NOT_FOUND;
  }

  auto req = rpc::make_shared_message<atfw::orbit::STCSendToClientReq>(ctx);

  *req->mutable_server_identity() = server_identity_;
  *req->mutable_client_identity() = client_info_ptr_->client_identity;
  *req->mutable_payload() = std::string(static_cast<const char*>(msg_data), msg_size);

  rpc::result_code_type::value_type res{};
  atframework::SSMsg* req_msg_ptr = ctx.create<atframework::SSMsg>();
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
                                    logic_config::me()->get_local_server_name());
  res = rpc::setup_rpc_stream_header(
      *req_msg.mutable_head()->mutable_rpc_stream(), "orbit.ServerToControllerService",
      "orbit.ServerToControllerService/send_to_client",
      {atfw::util::nostd::data(atfw::orbit::STCSendToClientReq::descriptor()->full_name()),
       atfw::util::nostd::size(atfw::orbit::STCSendToClientReq::descriptor()->full_name())});
  if (res < 0) {
    return static_cast<int32_t>(res);
  }

  if (!rpc::servertocontrollerservice::packer::pack_send_to_client(*req_msg.mutable_body_bin(), *req)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  rpc::context __child_ctx(ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcMethod, "orbit.ServerToControllerService/send_to_client"}};
  rpc::setup_rpc_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                        "orbit.ServerToControllerService/send_to_client", __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(controller_server_id, req_msg);
  if (res < 0) {
    FWLOGERROR("rpc {} call failed, res: {}({})", "orbit.ServerToControllerService/send_to_client", res,
               protobuf_mini_dumper_get_error_msg(res));
  }

  return __tracer.finish({res, __trace_attributes});
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_server_manager::send_to_client(rpc::context& ctx,
                                                                                    const std::string& client_id,
                                                                                    const void* msg_data,
                                                                                    size_t msg_size) {
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    FWLOGERROR("failed to find client info for client identity {}", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_FOUND);
  }

  if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_RUNNING) {
    FWLOGERROR("client {} is not in running status, current status: {}", client_id,
               static_cast<int>(client_info_ptr_->status));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_RUNNING);
  }

  uint64_t controller_server_id = select_controller_server_id(client_id, client_info_ptr_->region);
  if (controller_server_id == 0) {
    FWLOGERROR("failed to select controller server for region {}", client_info_ptr_->region);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CONTROLLER_SERVER_NOT_FOUND);
  }

  auto req = rpc::make_shared_message<atfw::orbit::STCSendToClientReq>(ctx);
  auto rsp = rpc::make_shared_message<atfw::orbit::CTSSendToClientRsp>(ctx);

  *req->mutable_server_identity() = server_identity_;
  *req->mutable_client_identity() = client_info_ptr_->client_identity;
  *req->mutable_payload() = std::string(static_cast<const char*>(msg_data), msg_size);

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::servertocontrollerservice::send_to_client(ctx, controller_server_id, *req, *rsp));
  if (rpc_result == 0) {
    rpc_result = rsp->error_code();
  }
  RPC_RETURN_CODE(rpc_result);
}

void orbit_server_manager::server_heartbeat() {
  if (last_heartbeat_time_ + static_cast<time_t>(heartbeat_interval_sec_) >
      atfw::util::time::time_utility::get_sys_now()) {
    return;
  }
  last_heartbeat_time_ = atfw::util::time::time_utility::get_sys_now();

  std::unordered_map<std::string, std::set<uint64_t>> region_agent_map;
  for (const auto& iter : client_region_map_) {
    // 同一Region一个协议
    for (const auto& client_info_iter : iter.second) {
      auto client_info_ptr_ = get_client_info(client_info_iter);
      if (client_info_ptr_ == nullptr) {
        continue;
      }
      if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_RUNNING &&
          client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_STARTING) {
        continue;
      }
      if (client_info_ptr_->client_identity.agent_identity().agent_server_id() == 0) {
        FWLOGERROR("client {} agent identity is not set", client_info_ptr_->client_id);
        continue;
      }
      region_agent_map[iter.first].insert(client_info_ptr_->client_identity.agent_identity().agent_server_id());
    }
  }

  for (auto& iter : region_agent_map) {
    uint64_t controller_server_id = select_controller_server_id(iter.first);
    if (controller_server_id == 0) {
      FWLOGERROR("failed to select controller server for region {}", iter.first);
      continue;
    }

    auto req = rpc::make_shared_message<atfw::orbit::STCServerHeartbeatNotify>(logic_server_get_current_tick_context());
    *req->mutable_server_identity() = server_identity_;
    for (auto agent_id : iter.second) {
      req->add_agent_identity()->set_agent_server_id(agent_id);
    }
    int32_t rpc_result = rpc::servertocontrollerservice::server_heartbeat(logic_server_get_current_tick_context(),
                                                                          controller_server_id, *req)
                             .unwrap();
    if (rpc_result < 0) {
      FWLOGERROR("orbit server_heartbeat failed to controller {:#x}, res: {}", controller_server_id, rpc_result);
    }
  }
}

void orbit_server_manager::check_client_timeout() {
  time_t now = atfw::util::time::time_utility::get_sys_now();
  while (!timeout_client_queue_.empty()) {
    auto& timeout_info = timeout_client_queue_.front();
    if (timeout_info.timeout_exit_time > now) {
      break;
    }
    timeout_client_queue_.pop_front();

    auto client_info_ptr_ = timeout_info.client_info_weak_ptr.lock();
    if (client_info_ptr_ != nullptr && client_info_ptr_->timeout_exit_time == timeout_info.timeout_exit_time) {
      FWLOGINFO("client {} timeout, last active time: {}, timeout exit time: {}", client_info_ptr_->client_id,
                client_info_ptr_->timeout_exit_time - client_timeout_sec_, client_info_ptr_->timeout_exit_time);
      client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_EXITED;
      erase_client_info(client_info_ptr_->client_id);
      if (on_client_end_notify_) {
        auto invoke_result = rpc::async_invoke(
            logic_server_get_current_tick_context(), "orbit_server_manager.check_client_timeout.on_client_end_notify_",
            [client_id = client_info_ptr_->client_id,
             on_client_end_notify = on_client_end_notify_](rpc::context& sub_ctx) mutable -> rpc::result_code_type {
              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(on_client_end_notify(
                  sub_ctx, client_id, atfw::orbit::EN_CLIENT_EXIT_REASON_HEARTBEAT_TIMEOUT, "", 0)));
            });
        if (!invoke_result.is_success()) {
          FWLOGERROR("orbit server failed to spawn async_notify_client_exit task for {}, res: {}({})",
                     client_info_ptr_->client_id, *invoke_result.get_error(),
                     protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
        }
      }
    }
  }
}

client_info_ptr orbit_server_manager::get_client_info(const std::string& client_id) {
  auto iter = client_info_map_.find(client_id);
  if (iter == client_info_map_.end()) {
    return nullptr;
  }
  return iter->second;
}

void orbit_server_manager::erase_client_info(const std::string& client_id) {
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    return;
  }
  client_info_map_.erase(client_id);
  auto client_region_iter = client_region_map_.find(client_info_ptr_->region);
  if (client_region_iter != client_region_map_.end()) {
    client_region_iter->second.erase(client_id);
    if (client_region_iter->second.empty()) {
      client_region_map_.erase(client_region_iter);
    }
  }
}

void orbit_server_manager::add_client_timeout(const client_info_ptr& client, time_t timeout_sec) {
  if (client == nullptr) {
    return;
  }
  if (timeout_sec <= 0) {
    timeout_sec = client_timeout_sec_;
  }
  client->timeout_exit_time = atfw::util::time::time_utility::get_sys_now() + timeout_sec;
  // 加入到超时检查内
  timeout_client_queue_.emplace_back(
      client_timeout_info{.client_info_weak_ptr = client, .timeout_exit_time = client->timeout_exit_time});
}

uint64_t orbit_server_manager::select_controller_server_id(const std::string& client_id, const std::string& region) {
  atfw::atapp::protocol::atapp_metadata controller_policy_selector_;
  (*controller_policy_selector_.mutable_labels())["orbit.region"] = region;

  auto* common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    return 0;
  }
  auto discovery = common_mod->get_discovery_index_by_type(
      static_cast<uint64_t>(atframework::component::logic_service_type::kOrbitControllerSvr));
  if (!discovery) {
    return 0;
  }

  auto selected = discovery->get_node_by_consistent_hash(client_id, &controller_policy_selector_);
  if (!selected) {
    return 0;
  }

  return selected->get_discovery_info().id();
}

uint64_t orbit_server_manager::select_controller_server_id(const std::string& region) {
  atfw::atapp::protocol::atapp_metadata controller_policy_selector_;
  (*controller_policy_selector_.mutable_labels())["orbit.region"] = region;

  auto* common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    return 0;
  }
  auto discovery = common_mod->get_discovery_index_by_type(
      static_cast<uint64_t>(atframework::component::logic_service_type::kOrbitControllerSvr));
  if (!discovery) {
    return 0;
  }

  auto selected = discovery->get_node_by_random(&controller_policy_selector_);
  if (!selected) {
    return 0;
  }

  return selected->get_discovery_info().id();
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_server_manager::handle_forward_to_server(
    rpc::context& ctx, const atfw::orbit::CTSForwardToServerReq& req) {
  const std::string& client_id = req.client_message().client_identity().client_id().client_id();
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    FWLOGERROR("failed to find client info for client identity {}", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_FOUND);
  }
  if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_RUNNING) {
    FWLOGERROR("client {} is not in running status, current status: {}", client_id,
               static_cast<int>(client_info_ptr_->status));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_RUNNING);
  }
  if (on_forward_to_server_) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(on_forward_to_server_(ctx, client_id, req.client_message().payload())));
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_server_manager::handle_client_start_notify(
    rpc::context& ctx, const atfw::orbit::CTSClientStartReq& req) {
  const std::string& client_id = req.client_identity().client_id().client_id();
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    FWLOGERROR("not found client info for client identity {}", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_FOUND);
  }
  if (client_info_ptr_->status == EnClientStatus::EN_CLIENT_STATUS_RUNNING) {
    FWLOGINFO("client {} already in running status, current status: {}", client_id,
              static_cast<int>(client_info_ptr_->status));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_STARTING &&
      client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_START_CONFIRMING) {
    FWLOGERROR("client {} is not in starting status, current status: {}", client_id,
               static_cast<int>(client_info_ptr_->status));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_RUNNING);
  }
  client_info_ptr_->client_identity = req.client_identity();
  client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_RUNNING;
  add_client_timeout(client_info_ptr_);
  if (on_client_start_notify_) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(on_client_start_notify_(ctx, client_id, req.client_addr(), req.data())));
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_server_manager::handle_client_end_notify(
    rpc::context& ctx, const atfw::orbit::CTSClientEndReq& req) {
  const std::string& client_id = req.client_identity().client_id().client_id();
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    FWLOGERROR("failed to find client info for client identity {}", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_FOUND);
  }

  erase_client_info(client_id);
  if (on_client_end_notify_) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
        on_client_end_notify_(ctx, client_id, req.exit_reason(), req.exit_data(), req.exit_code())));
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_SERVICE_API rpc::result_code_type orbit_server_manager::handle_client_agent_heartbeat_notify(
    ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx, const atfw::orbit::CTSClientAgentHeartbeatNotify& req) {
  for (const auto& client_id : req.client_ids()) {
    const std::string& id = client_id.client_id();
    auto client_info_ptr_ = get_client_info(id);
    if (client_info_ptr_ == nullptr) {
      FWLOGERROR("failed to find client info for client identity {}", id);
      continue;
    }
    // 刷新心跳时间 非running 来检查启动超时
    if (client_info_ptr_->status == EnClientStatus::EN_CLIENT_STATUS_RUNNING) {
      add_client_timeout(client_info_ptr_);
    }
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
