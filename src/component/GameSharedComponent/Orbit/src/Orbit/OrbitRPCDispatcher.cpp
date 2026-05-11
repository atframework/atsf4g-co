#include <Orbit/OrbitRPCDispatcher.h>

#include <Orbit/OrbitClientRuntime.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/client_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {

OrbitRPCDispatcher::OrbitRPCDispatcher() : sequence_allocator_(0) {}

void OrbitRPCDispatcher::init() {
  sequence_allocator_ = static_cast<uint64_t>((::util::time::time_utility::get_sys_now() - 1577836800) << 23) +
                        static_cast<uint64_t>(::util::time::time_utility::get_now_usec() << 3);
}

void OrbitRPCDispatcher::tick() { rsp_callback_execute(); }

bool OrbitRPCDispatcher::check_rpc_success() {
  rsp_callback_execute();
  for (const auto& iter : timeout_callback_map_) {
    if (iter.second.second.lock() != nullptr) {
      return false;
    }
  }
  return true;
}

const std::string& OrbitRPCDispatcher::pick_rpc_name(const orbit::OrbitRpcMessage& raw_msg) {
  if (!raw_msg.has_head()) {
    return get_empty_string();
  }
  if (raw_msg.head().has_rpc_request()) {
    return raw_msg.head().rpc_request().rpc_name();
  }
  if (raw_msg.head().has_rpc_stream()) {
    return raw_msg.head().rpc_stream().rpc_name();
  }
  return get_empty_string();
}

int32_t OrbitRPCDispatcher::dispatch(const std::string& message) {
  orbit::OrbitRpcMessage orbit_msg;
  if (!orbit_msg.ParseFromString(message)) {
    OrbitClientRuntime::me()->log(OrbitClientLogLevel::kError,
                                  LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] parse message failed.\n{}", message));
    return orbit::EN_ORBIT_ERROR_CODE_PARSE_MESSAGE_FAILED;
  }
  if (!orbit_msg.has_head()) {
    OrbitClientRuntime::me()->log(OrbitClientLogLevel::kError,
                                  LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] message head not found.\n{}", message));
    return orbit::EN_ORBIT_ERROR_CODE_MESSAGE_HEAD_NOT_FOUND;
  }
  if (orbit_msg.head().has_rpc_request() || orbit_msg.head().has_rpc_stream()) {
    return on_rpc_req_message(orbit_msg);
  } else if (orbit_msg.head().has_rpc_response()) {
    return on_rpc_rsp_message(orbit_msg);
  } else {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kError,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] unknown message.\n{}",
                                 OrbitClientRuntime::protobuf_mini_dumper_get_readable(orbit_msg)));
    return orbit::EN_ORBIT_ERROR_CODE_PARSE_MESSAGE_FAILED;
  }
}

int32_t OrbitRPCDispatcher::on_rpc_req_message(orbit::OrbitRpcMessage& orbit_msg) {
  int32_t ret = 0;
  do {
    OrbitClientRuntime::me()->log(
        OrbitClientLogLevel::kInfo,
        LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] recv req msg {} bytes\n{}", orbit_msg.ByteSizeLong(),
                                 OrbitClientRuntime::protobuf_mini_dumper_get_readable(orbit_msg)));
    const std::string& rpc_name = pick_rpc_name(orbit_msg);
    if (rpc_name.empty()) {
      ret = orbit::EN_ORBIT_ERROR_CODE_RPC_NAME_NOTFOUND;
      break;
    }
    if (task_action_map_by_name_.empty()) {
      ret = orbit::EN_ORBIT_ERROR_CODE_TASK_ACTION_EMPTY;
      break;
    }
    rpc_task_action_set_t::iterator iter = task_action_map_by_name_.find(rpc_name);
    if (task_action_map_by_name_.end() != iter && iter->second) {
      return (*iter->second)(std::move(orbit_msg));
    }
    ret = orbit::EN_ORBIT_ERROR_CODE_TASK_ACTION_NOTFOUND;
  } while (false);
  on_create_task_failed(orbit_msg, ret);
  return ret;
}

int32_t OrbitRPCDispatcher::on_rpc_rsp_message(orbit::OrbitRpcMessage& orbit_msg) {
  OrbitClientRuntime::me()->log(
      OrbitClientLogLevel::kInfo,
      LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] recv rsp msg.\n{}",
                               OrbitClientRuntime::protobuf_mini_dumper_get_readable(orbit_msg)));
  uint64_t sequence = orbit_msg.head().destination_task_id();
  if (sequence == 0) {
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }
  auto iter = sequence_callback_map_.find(sequence);
  if (iter == sequence_callback_map_.end()) {
    return 0;  // MAYBE TIMEOUT
  }
  rsp_callback_t callback = *(iter->second);
  sequence_callback_map_.erase(iter);
  if (callback != nullptr) {
    callback(orbit_msg);
  }
  return 0;
}

uint64_t OrbitRPCDispatcher::allocate_sequence() { return ++sequence_allocator_; }

int32_t OrbitRPCDispatcher::send_rsp_to_proc(orbit::OrbitRpcMessage& orbit_msg) {
  if (0 == orbit_msg.head().sequence()) {
    orbit_msg.mutable_head()->set_sequence(allocate_sequence());
  }
  size_t msg_buf_len = orbit_msg.ByteSizeLong();
  OrbitClientRuntime::me()->log(
      OrbitClientLogLevel::kInfo,
      LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] send rsp msg to {} bytes\n{}", msg_buf_len,
                               OrbitClientRuntime::protobuf_mini_dumper_get_readable(orbit_msg)));
  OrbitClientRequestOptions request_options;
  request_options.reliable = true;
  request_options.retry_times = 3;
  return OrbitClientRuntime::me()->send_to_server(orbit_msg.SerializeAsString(), nullptr, request_options);
}

int32_t OrbitRPCDispatcher::send_req_to_proc(orbit::OrbitRpcMessage& orbit_msg, uint64_t& sequence,
                                             const OrbitClientRequestOptions& request_options) {
  if (0 == orbit_msg.head().sequence()) {
    orbit_msg.mutable_head()->set_sequence(allocate_sequence());
  }
  orbit_msg.mutable_head()->set_source_task_id(orbit_msg.head().sequence());

  size_t msg_buf_len = orbit_msg.ByteSizeLong();
  OrbitClientRuntime::me()->log(
      OrbitClientLogLevel::kInfo,
      LOG_WRAPPER_FWAPI_FORMAT("[ORBIT_RPC] send req msg to {} bytes\n{}", msg_buf_len,
                               OrbitClientRuntime::protobuf_mini_dumper_get_readable(orbit_msg)));
  sequence = orbit_msg.head().sequence();
  return OrbitClientRuntime::me()->send_to_server(orbit_msg.SerializeAsString(), nullptr, request_options);
}

int OrbitRPCDispatcher::_register_action(const std::string& rpc_full_name, task_action_creator_t action) {
  rpc_task_action_set_t::iterator iter = task_action_map_by_name_.find(rpc_full_name);
  if (task_action_map_by_name_.end() != iter) {
    return orbit::EN_ORBIT_ERROR_CODE_ALREADY_REGISTER_ACTION;
  }
  task_action_map_by_name_[rpc_full_name] = action;
  return 0;
}

void OrbitRPCDispatcher::on_create_task_failed(orbit::OrbitRpcMessage& orbit_msg, int32_t ret_code) {
  if (!orbit_msg.has_head()) {
    return;
  }

  if (!orbit_msg.head().has_rpc_request() || 0 == orbit_msg.head().source_task_id()) {
    return;
  }

  orbit::OrbitRpcMessage rsp;
  auto rsp_head = rsp.mutable_head();

  rsp_head->set_source_task_id(0);
  rsp_head->set_destination_task_id(orbit_msg.head().source_task_id());
  rsp_head->set_sequence(orbit_msg.head().sequence());
  rsp_head->set_error_code(ret_code);
  rsp_head->set_timestamp(::util::time::time_utility::get_sys_now());

  rsp_head->mutable_rpc_response()->set_rpc_name(orbit_msg.head().rpc_request().rpc_name());

  send_rsp_to_proc(rsp);
}

int32_t OrbitRPCDispatcher::init_rpc_req_callback(uint64_t sequence, time_t timeout, rsp_callback_t callback) {
  if (sequence == 0) {
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }
  // 注册回调
  if (sequence_callback_map_.find(sequence) != sequence_callback_map_.end()) {
    return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
  }
  time_t timeout_stamp = ::util::time::time_utility::get_sys_now() + timeout;
  auto ptr = std::make_shared<rsp_callback_t>(callback);
  sequence_callback_map_[sequence] = ptr;
  timeout_callback_map_.insert(
      std::pair(timeout_stamp, std::pair<uint64_t, std::weak_ptr<rsp_callback_t>>(sequence, ptr)));
  return 0;
}

void OrbitRPCDispatcher::rsp_callback_execute() {
  time_t current_time = ::util::time::time_utility::get_sys_now();
  for (auto iter = timeout_callback_map_.begin(); iter != timeout_callback_map_.end();) {
    if (iter->first >= current_time) {
      break;
    }
    if (!iter->second.second.expired()) {
      rsp_callback_t callback = *(iter->second.second.lock());
      // REMOVE SEQ
      sequence_callback_map_.erase(iter->second.first);
      if (callback != nullptr) {
        // CALL TIMEOUT
        orbit::OrbitRpcMessage orbit_msg;
        orbit_msg.mutable_head()->set_error_code(orbit::EN_ORBIT_ERROR_CODE_TIMEOUT);
        callback(orbit_msg);
      }
    }
    timeout_callback_map_.erase(iter);
    iter = timeout_callback_map_.begin();
  }
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END