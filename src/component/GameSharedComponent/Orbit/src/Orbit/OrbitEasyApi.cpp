// Copyright 2026 atframework
#include <Orbit/OrbitEasyApi.h>

#include <Orbit/OrbitClientRuntime.h>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk_easy_api {

ORBIT_CLIENT_SDK_API int init(int argc, char* argv[], bool io_thread, const orbit_client_sdk::OrbitClientCallbacks& callbacks) {
  return orbit_client_sdk::OrbitClientRuntime::me()->init(argc, argv, io_thread, callbacks);
}
ORBIT_CLIENT_SDK_API void init_task_handler_private_data_callback(std::function<void*()> callback) {
  orbit_client_sdk::OrbitRPCDispatcher::me()->init_task_handler_private_data_callback(callback);
}
ORBIT_CLIENT_SDK_API void tick() { orbit_client_sdk::OrbitClientRuntime::me()->tick(); }
ORBIT_CLIENT_SDK_API bool enabled() { return orbit_client_sdk::OrbitClientRuntime::me()->enabled(); }
ORBIT_CLIENT_SDK_API bool is_seed_process() { return orbit_client_sdk::OrbitClientRuntime::me()->is_seed_process(); }
ORBIT_CLIENT_SDK_API const std::vector<std::string>& get_custom_launch_arguments() {
  return orbit_client_sdk::OrbitClientRuntime::me()->get_custom_launch_arguments();
}
ORBIT_CLIENT_SDK_API const std::string& find_custom_launch_argument(const std::string& key) {
  return orbit_client_sdk::OrbitClientRuntime::me()->find_custom_launch_argument(key);
}

ORBIT_CLIENT_SDK_API int32_t notify_seed_process_ready() {
  return orbit_client_sdk::OrbitClientRuntime::me()->notify_seed_process_ready();
}
ORBIT_CLIENT_SDK_API int32_t blocking_seed_process() {
  return orbit_client_sdk::OrbitClientRuntime::me()->blocking_seed_process();
}
ORBIT_CLIENT_SDK_API int32_t notify_process_ready(const std::string& client_addr, const std::string& custom_data) {
  return orbit_client_sdk::OrbitClientRuntime::me()->notify_process_ready(client_addr, custom_data);
}
ORBIT_CLIENT_SDK_API int32_t request_end(orbit::EnClientExitReason reason, int32_t exit_code,
                                         const std::string& custom_data) {
  return orbit_client_sdk::OrbitClientRuntime::me()->request_end(reason, exit_code, custom_data);
}

}  // namespace orbit_client_sdk_easy_api

ORBIT_CLIENT_SDK_NAMESPACE_END