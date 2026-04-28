#include "external_client.pseudo.h"

namespace atorbit {
namespace sdk {
namespace external {

namespace {

static bool same_ds_identity(const ds_identity_t& left, const ds_identity_t& right) {
  return left.dsa_id == right.dsa_id && left.ds_id == right.ds_id;
}

}  // namespace

result_code_t external_client::connect(const char* dsc_region, unique_id_t unique_id) {
  if (nullptr == dsc_region || 0 == unique_id) {
    return -1;
  }

  // 读取目标 Region，并通过 libatapp connector 建立到 DSC 的连接
  bound_region_ = dsc_region;
  unique_id_ = unique_id;
  connection_state_ = connection_state_t::k_connected;
  next_request_id_ = 1;
  clear_all_routes();

  // 发送 ConnectExternal RPC，建立 service 会话
  // 若连接失败，返回错误码；若成功，缓存当前会话状态
  return 0;
}

launch_result_t external_client::launch_dedicated_server(const launch_request_t& request) {
  launch_result_t result;
  if (ensure_connected() != 0 || !validate_launch_request(request)) {
    result.result_code = -1;
    return result;
  }

  auto request_id = allocate_request_id();

  // 组装 LaunchDedicatedServer RPC 请求
  // 请求中带上当前会话、目标 Region、期望资源和 custom args
  // 发送到 DSC，等待 LaunchDedicatedServerRsp
  // DSC 合约保证: 返回成功时，DS 已完成 DS SDK -> DSA 的注册与 ready 握手

  result.result_code = 0;
  result.ds.dsa_id = 10000 + request_id;
  result.ds.ds_id = 20000 + request_id;
  result.client_addr = "pending://client_addr_from_dsc";

  auto* route = bind_ready_ds(result.ds, result.client_addr);
  if (nullptr == route) {
    result.result_code = -2;
    return result;
  }

  return result;
}

result_code_t external_client::send_to_ds(const ds_identity_t& ds, message_buffer_t payload) {
  if (ensure_connected() != 0) {
    return -1;
  }

  if (nullptr == payload) {
    return -2;
  }

  auto* route = find_owned_ds(ds);
  if (nullptr == route || route->route_state != static_cast<int>(ds_route_state_t::k_ready)) {
    return -3;
  }

  // 组装 SendToDS RPC 请求，并带上目标 dsa_id / ds_id
  // 发送给 DSC，由 DSC 再转发给目标 DSA / DS
  // 若需要 ACK，则等待下行确认结果
  return 0;
}

void external_client::set_ds_message_callback(ds_message_callback_t callback) {
  event_stream_.set_ds_message_callback(callback);
}

result_code_t external_client::handle_transport_disconnected(int disconnect_reason) {
  if (connection_state_ == connection_state_t::k_disconnected) {
    return 0;
  }

  connection_state_ = connection_state_t::k_recovering;
  mark_all_routes_disconnected(disconnect_reason);
  connection_state_ = connection_state_t::k_disconnected;
  return 0;
}

void external_client::set_ds_disconnect_callback(ds_disconnect_callback_t callback) {
  event_stream_.set_ds_disconnect_callback(callback);
}

void external_client::set_ds_exit_callback(ds_exit_callback_t callback) {
  event_stream_.set_ds_exit_callback(callback);
}

result_code_t external_client::disconnect() {
  // 关闭与 DSC 的连接，并清理本地会话状态
  clear_all_routes();
  connection_state_ = connection_state_t::k_disconnected;
  bound_region_ = nullptr;
  unique_id_ = 0;
  return 0;
}

result_code_t external_client::ensure_connected() const {
  if (connection_state_ != connection_state_t::k_connected || nullptr == bound_region_ || 0 == unique_id_) {
    return -1;
  }

  return 0;
}

bool external_client::validate_launch_request(const launch_request_t& request) const {
  if (nullptr == request.target_region || request.expected_cpu <= 0 || request.expected_memory_mb <= 0) {
    return false;
  }

  return true;
}

unsigned long long external_client::allocate_request_id() {
  return next_request_id_++;
}

external_client::owned_ds_route_t* external_client::find_owned_ds(const ds_identity_t& ds) {
  for (auto& route : owned_ds_routes_) {
    if (route.occupied && same_ds_identity(route.ds, ds)) {
      return &route;
    }
  }

  return nullptr;
}

external_client::owned_ds_route_t* external_client::bind_ready_ds(const ds_identity_t& ds, const char* client_addr) {
  auto* existing = find_owned_ds(ds);
  if (nullptr != existing) {
    existing->client_addr = client_addr;
    existing->route_state = static_cast<int>(ds_route_state_t::k_ready);
    return existing;
  }

  for (auto& route : owned_ds_routes_) {
    if (!route.occupied) {
      route.occupied = true;
      route.ds = ds;
      route.client_addr = client_addr;
      route.route_state = static_cast<int>(ds_route_state_t::k_ready);
      return &route;
    }
  }

  return nullptr;
}

void external_client::mark_all_routes_disconnected(int disconnect_reason) {
  for (auto& route : owned_ds_routes_) {
    if (!route.occupied) {
      continue;
    }

    route.route_state = static_cast<int>(ds_route_state_t::k_disconnected);
    event_stream_.dispatch_ds_disconnect(route.ds, disconnect_reason);
  }
}

void external_client::clear_all_routes() {
  for (auto& route : owned_ds_routes_) {
    route.occupied = false;
    route.ds.dsa_id = 0;
    route.ds.ds_id = 0;
    route.client_addr = nullptr;
    route.route_state = static_cast<int>(ds_route_state_t::k_empty);
  }
}
}  // namespace external
}  // namespace sdk
}  // namespace atorbit
