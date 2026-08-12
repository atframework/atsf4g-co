#pragma once

#include "OrbitClientSdkTypes.h"

#include <design_pattern/singleton.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/descriptor.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <Orbit/OrbitConfig.h>

#include <nostd/string_view.h>
#include <string/string_format.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace orbit {
class OrbitRpcMessage;
}

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN
namespace orbit_client_sdk {

struct task_action_maker_base_t {
  explicit task_action_maker_base_t() {};
  virtual ~task_action_maker_base_t() {};
  virtual int operator()(void *private_data, atfw::orbit::OrbitRpcMessage &&orbit_msg) = 0;
};

template <typename TAction>
struct task_action_maker_t : public task_action_maker_base_t {
  int operator()(void *private_data, atfw::orbit::OrbitRpcMessage &&orbit_msg) override {
    auto ptr = std::make_shared<TAction>(private_data, std::move(orbit_msg));
    return (*ptr)();
  };
};

class OrbitRPCDispatcher {
  using msg_type_t = uint32_t;
  using task_action_creator_t = std::shared_ptr<task_action_maker_base_t>;
  using rpc_task_action_set_t = std::unordered_map<std::string, task_action_creator_t>;

  using rsp_callback_t = std::function<void(const atfw::orbit::OrbitRpcMessage &)>;

#if defined(ORBIT_CLIENT_SDK_DLL) && ORBIT_CLIENT_SDK_DLL
#  if defined(ORBIT_CLIENT_SDK_NATIVE) && ORBIT_CLIENT_SDK_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(OrbitRPCDispatcher)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(OrbitRPCDispatcher)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(OrbitRPCDispatcher)
#endif

 private:
  ORBIT_CLIENT_SDK_API OrbitRPCDispatcher();

 public:
  ORBIT_CLIENT_SDK_API virtual ~OrbitRPCDispatcher();

  ORBIT_CLIENT_SDK_API void init();
  ORBIT_CLIENT_SDK_API void tick();

  ORBIT_CLIENT_SDK_API bool check_rpc_success();

  ORBIT_CLIENT_SDK_API const std::string &pick_rpc_name(const atfw::orbit::OrbitRpcMessage &raw_msg);

  ORBIT_CLIENT_SDK_API int32_t dispatch(const std::string &message);
  ORBIT_CLIENT_SDK_API int32_t on_rpc_req_message(atfw::orbit::OrbitRpcMessage &orbit_msg);
  ORBIT_CLIENT_SDK_API int32_t on_rpc_rsp_message(atfw::orbit::OrbitRpcMessage &orbit_msg);

  ORBIT_CLIENT_SDK_API uint64_t allocate_sequence();

  template <typename TAction>
  ATFW_UTIL_FORCEINLINE task_action_creator_t make_task_creator() {
    return std::make_shared<task_action_maker_t<TAction>>();
  }

  ORBIT_CLIENT_SDK_API int32_t init_rpc_req_callback(uint64_t sequence, time_t timeout, rsp_callback_t callback);

  ORBIT_CLIENT_SDK_API void init_task_handler_private_data_callback(std::function<void *()> callback);

  template <typename TAction>
  ATFW_UTIL_SYMBOL_VISIBLE int register_action(const ::google::protobuf::ServiceDescriptor *service_desc,
                                               const std::string &rpc_name, bool allow_after_init) {
    if (nullptr == service_desc) {
      return atfw::orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
    }
    if (sequence_allocator_ != 0 && !allow_after_init) {
      return atfw::orbit::EN_ORBIT_ERROR_CODE_CALL_AFTER_INIT;
    }
    std::string service_full_name = std::string{service_desc->full_name()};
    const ::google::protobuf::MethodDescriptor *method = service_desc->FindMethodByName(rpc_name);
    if (nullptr == method) {
      return atfw::orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
    }
    std::string method_full_name_otel = atfw::util::string::format(
        "{}/{}", service_full_name, atfw::util::nostd::string_view{method->name().data(), method->name().size()});
    return _register_action(method_full_name_otel, make_task_creator<TAction>());
  }

 public:
  ORBIT_CLIENT_SDK_API int32_t send_rsp_to_proc(atfw::orbit::OrbitRpcMessage &orbit_msg);
  ORBIT_CLIENT_SDK_API int32_t
  send_req_to_proc(atfw::orbit::OrbitRpcMessage &orbit_msg, uint64_t &sequence,
                   const OrbitClientRequestOptions &request_options = OrbitClientRequestOptions{});

  ATFW_UTIL_FORCEINLINE const std::string &get_empty_string() {
    static std::string ret;
    return ret;
  }

 private:
  ORBIT_CLIENT_SDK_API int _register_action(const std::string &rpc_full_name, task_action_creator_t action);
  void on_create_task_failed(atfw::orbit::OrbitRpcMessage &orbit_msg, int32_t ret_code);
  void rsp_callback_execute();

  uint64_t sequence_allocator_ = 0;
  rpc_task_action_set_t task_action_map_by_name_;
  std::function<void *()> private_data_callback_;

  std::map<uint64_t, std::shared_ptr<rsp_callback_t>> sequence_callback_map_;
  std::multimap<time_t, std::pair<uint64_t, std::weak_ptr<rsp_callback_t>>> timeout_callback_map_;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END