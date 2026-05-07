#include "orbit_server_manager.h"

#include "handle/handle_ss_rpc_controllertoserverservice.h"

#include <logic/logic_server_macro.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/servertocontrollerservice/servertocontrollerservice.h>

int orbit_server_manager::init(uint64_t unique_id, uint64_t heartbeat_interval_sec) {
  server_identity_.set_unique_id(unique_id);
  server_identity_.set_server_node_id(logic_config::me()->get_local_server_id());
  heartbeat_interval_sec_ = heartbeat_interval_sec;
  if (heartbeat_interval_sec_ <= 0) {
    heartbeat_interval_sec_ = 5;
  }

  INIT_CALL_FN(handle::controllertoserverservice::register_handles_for_controllertoserverservice);
  return 0;
}

void orbit_server_manager::stop() {}

void orbit_server_manager::tick() { server_heartbeat(); }

rpc::result_code_type orbit_server_manager::start_client(
    rpc::context& ctx, const std::string& region, const orbit::DAgentClientStartArgs& args,
    const google::protobuf::RepeatedPtrField<std::string>& match_tags) {
  const std::string& client_id = args.client_start_args().client_id().client_id();
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

  client_info_ptr_ = atfw::util::memory::make_strong_rc<client_info>();
  client_info_ptr_->client_id = client_id;
  client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_STARTING;
  client_info_ptr_->region = region;
  client_info_map_[client_id] = client_info_ptr_;

  auto req = rpc::make_shared_message<orbit::STCLaunchClientReq>(ctx);
  auto rsp = rpc::make_shared_message<orbit::CTSLaunchClientRsp>(ctx);

  *req->mutable_server_identity() = server_identity_;
  *req->mutable_args() = args;
  *req->mutable_match_tags() = match_tags;

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::servertocontrollerservice::launch_client(ctx, controller_server_id, *req, *rsp));
  if (rpc_result < 0) {
    FWLOGERROR("orbit launch_client failed for {} to controller {:#x}, res: {}", client_id, controller_server_id,
               rpc_result);
    client_info_ptr_->status = EnClientStatus::EN_CLIENT_STATUS_EXITED;
    client_info_map_.erase(client_id);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_server_manager::send_to_client(rpc::context& ctx, const std::string& client_id,
                                                           const std::string& data) {
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

  auto req = rpc::make_shared_message<orbit::STCSendToClientNotify>(ctx);

  *req->mutable_server_identity() = server_identity_;
  *req->mutable_client_identity() = client_info_ptr_->client_identity;
  *req->mutable_payload() = data;

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(rpc::servertocontrollerservice::send_to_client(ctx, controller_server_id, *req));
  if (rpc_result < 0) {
    FWLOGERROR("orbit send_to_client failed for {} to controller {:#x}, res: {}", client_id, controller_server_id,
               rpc_result);
    RPC_RETURN_CODE(rpc_result);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
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
      if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_RUNNING) {
        continue;
      }
      region_agent_map[iter.first].insert(client_info_ptr_->client_identity.agent_identity().agent_server_id());
    }
  }

  rpc::context ctx{rpc::context::create_without_task()};
  for (auto& iter : region_agent_map) {
    uint64_t controller_server_id = select_controller_server_id(iter.first);
    if (controller_server_id == 0) {
      FWLOGERROR("failed to select controller server for region {}", iter.first);
      continue;
    }

    auto invoke_task = rpc::async_invoke(
        ctx, "orbit_server_manager::server_heartbeat",
        [this, controller_server_id,
         agent_id_set = std::move(iter.second)](rpc::context& child_ctx) -> rpc::result_code_type {
          auto req = rpc::make_shared_message<orbit::STCServerHeartbeatNotify>(child_ctx);
          *req->mutable_server_identity() = server_identity_;
          for (auto agent_id : agent_id_set) {
            req->add_agent_identity()->set_agent_server_id(agent_id);
          }

          int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
              rpc::servertocontrollerservice::server_heartbeat(child_ctx, controller_server_id, *req));
          if (rpc_result < 0) {
            FWLOGERROR("orbit server_heartbeat failed to controller {:#x}, res: {}", controller_server_id, rpc_result);
          }
          RPC_RETURN_CODE(rpc_result);
        });
    if (invoke_task.is_error()) {
      FWLOGERROR("orbit_server_manager::server_heartbeat invoke_task failed");
    }
  }
}

client_info_ptr orbit_server_manager::get_client_info(std::string client_id) {
  auto iter = client_info_map_.find(client_id);
  if (iter == client_info_map_.end()) {
    return nullptr;
  }
  return iter->second;
}

uint64_t orbit_server_manager::select_controller_server_id(const std::string& client_id, const std::string& region) {
  atfw::atapp::protocol::atapp_metadata controller_policy_selector_;
  (*controller_policy_selector_.mutable_labels())["orbit.region"] = region;

  auto common_mod = logic_server_last_common_module();
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

  auto common_mod = logic_server_last_common_module();
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

rpc::result_code_type orbit_server_manager::handle_forward_to_server(rpc::context& ctx,
                                                                     const orbit::CTSForwardToServerNotify& req) {
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

rpc::result_code_type orbit_server_manager::handle_client_start_notify(rpc::context& ctx,
                                                                       const orbit::CTSClientStartNotify& req) {
  const std::string& client_id = req.client_identity().client_id().client_id();
  auto client_info_ptr_ = get_client_info(client_id);
  if (client_info_ptr_ == nullptr) {
    FWLOGERROR("not found client info for client identity {}", client_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_FOUND);
  }
  if (client_info_ptr_->status != EnClientStatus::EN_CLIENT_STATUS_STARTING) {
    FWLOGERROR("client {} is not in starting status, current status: {}", client_id,
               static_cast<int>(client_info_ptr_->status));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SERVER_CLIENT_NOT_RUNNING);
  }
  client_info_ptr_->client_identity = req.client_identity();
  if (on_client_start_notify_) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(on_client_start_notify_(ctx, client_id, req.client_addr(), req.data())));
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

rpc::result_code_type orbit_server_manager::handle_client_end_notify(rpc::context& ctx,
                                                                     const orbit::CTSClientEndNotify& req) {
  const std::string& client_id = req.client_identity().client_id().client_id();
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
  client_info_map_.erase(client_id);
  if (on_client_end_notify_) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
        on_client_end_notify_(ctx, client_id, req.exit_reason(), req.exit_data(), req.exit_code())));
  }
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
