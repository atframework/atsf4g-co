// Copyright 2026 atframework

#pragma once

#include "OrbitClientSdkTypes.h"
#include "OrbitRPCDispatcher.h"
#include "OrbitRPCHandle.h"

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk_easy_api {

ORBIT_CLIENT_SDK_API int init(int argc, char* argv[], bool io_thread,
                              const orbit_client_sdk::OrbitClientCallbacks& callbacks);
ORBIT_CLIENT_SDK_API void init_task_handler_private_data_callback(std::function<void*()> callback);
ORBIT_CLIENT_SDK_API void tick();
ORBIT_CLIENT_SDK_API bool enabled();
ORBIT_CLIENT_SDK_API bool is_seed_process();
ORBIT_CLIENT_SDK_API const std::vector<std::string>& get_custom_launch_arguments();
ORBIT_CLIENT_SDK_API const std::string& find_custom_launch_argument(const std::string& key);

ORBIT_CLIENT_SDK_API int32_t notify_seed_process_ready();
ORBIT_CLIENT_SDK_API int32_t blocking_seed_process();
ORBIT_CLIENT_SDK_API int32_t notify_process_ready(const std::string& client_addr,
                                                  const std::string& custom_data = std::string{});
ORBIT_CLIENT_SDK_API int32_t request_end(atfw::orbit::EnClientExitReason reason, int32_t exit_code,
                                         const std::string& custom_data = std::string{});

}  // namespace orbit_client_sdk_easy_api

ORBIT_CLIENT_SDK_NAMESPACE_END

#define ORBIT_STRINGIFY_HELPER(x) #x
#define ORBIT_CONCAT_HELPER(x, y) x##y

#define ORBIT_RPC_HANDLE(orbit_rpc_name, orbit_service_name, orbit_rpc_req_type, orbit_rpc_rsp_type)            \
  int orbit_rpc_name(const orbit_rpc_req_type& req_body,                                                        \
                     std::function<void(int32_t, const orbit_rpc_rsp_type&)> callback, int32_t retry_time) {    \
    return ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::orbit_rpc_handle(                                   \
        ORBIT_STRINGIFY_HELPER(orbit_rpc_name), ORBIT_STRINGIFY_HELPER(orbit_service_name), req_body, callback, \
        retry_time);                                                                                            \
  }

#define ORBIT_REGISTER_ACTION_CODE(orbit_rpc_name, orbit_service_name, rpc_name)                                    \
  int ret = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitRPCDispatcher::me()                               \
                ->register_action<ORBIT_CONCAT_HELPER(task_action_, orbit_rpc_name)>(                               \
                    orbit_service_name::descriptor(), ORBIT_STRINGIFY_HELPER(rpc_name), true);                      \
  if (ret != 0) {                                                                                                   \
    ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientRuntime::me()->log(                                 \
        ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::OrbitClientLogLevel::kError, __FILE__, __LINE__,           \
        LOG_WRAPPER_FWAPI_FORMAT("register_orbit_rpc_action register action {} failed, ret: [{}]", #orbit_rpc_name, \
                                 ret));                                                                             \
  }

#define ORBIT_TASK_ACTION(orbit_rpc_name, orbit_rpc_req_type, orbit_rpc_rsp_type)                                      \
  class ORBIT_CONCAT_HELPER(task_action_, orbit_rpc_name)                                                              \
      : public ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::task_action_orbit_rpc_base<orbit_rpc_req_type,         \
                                                                                           orbit_rpc_rsp_type>,        \
        public std::enable_shared_from_this<ORBIT_CONCAT_HELPER(task_action_, orbit_rpc_name)> {                       \
    using base_type = ORBIT_CLIENT_SDK_NAMESPACE_ID::orbit_client_sdk::task_action_orbit_rpc_base<orbit_rpc_req_type,  \
                                                                                                  orbit_rpc_rsp_type>; \
                                                                                                                       \
   public:                                                                                                             \
    explicit ORBIT_CONCAT_HELPER(task_action_, orbit_rpc_name)(void* private_data, atfw::orbit::OrbitRpcMessage&& ds_msg)    \
        : base_type(std::move(ds_msg)), private_data_(private_data) {}                                                 \
    int operator()() {                                                                                                 \
      set_rsp_code(hook_run(private_data_, get_request_body(), get_response_body()));                                  \
      send_response();                                                                                                 \
      return 0;                                                                                                        \
    }                                                                                                                  \
    int hook_run(void* private_data, const rpc_request_type& req_body, rpc_response_type& rsp_body);                   \
                                                                                                                       \
   private:                                                                                                            \
    void* private_data_;                                                                                               \
  };