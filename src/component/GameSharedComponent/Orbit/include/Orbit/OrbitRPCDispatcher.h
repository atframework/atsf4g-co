#pragma once

#include <Orbit/OrbitClientSdkTypes.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/descriptor.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace orbit {
class OrbitRpcMessage;
}

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN
namespace orbit_client_sdk {

struct task_action_maker_base_t {
  explicit task_action_maker_base_t() {};
  virtual ~task_action_maker_base_t() {};
  virtual int operator()(orbit::OrbitRpcMessage &&orbit_msg) = 0;
};

template <typename TAction>
struct task_action_maker_t : public task_action_maker_base_t {
  int operator()(orbit::OrbitRpcMessage &&orbit_msg) override {
    auto ptr = std::make_shared<TAction>(std::move(orbit_msg));
    return (*ptr)();
  };
};

class ORBIT_CLIENT_SDK_API OrbitRPCDispatcher : public util::design_pattern::singleton<OrbitRPCDispatcher> {
  using msg_type_t = uint32_t;
  using task_action_creator_t = std::shared_ptr<task_action_maker_base_t>;
  using rpc_task_action_set_t = std::unordered_map<std::string, task_action_creator_t>;

  using rsp_callback_t = std::function<void(const orbit::OrbitRpcMessage &)>;

 public:
  OrbitRPCDispatcher();

  void init();
  void tick();

  bool check_rpc_success();

  const std::string &pick_rpc_name(const orbit::OrbitRpcMessage &raw_msg);

  int32_t dispatch(const std::string &message);
  int32_t on_rpc_req_message(orbit::OrbitRpcMessage &orbit_msg);
  int32_t on_rpc_rsp_message(orbit::OrbitRpcMessage &orbit_msg);

  uint64_t allocate_sequence();

  template <typename TAction>
  inline task_action_creator_t make_task_creator() {
    return std::make_shared<task_action_maker_t<TAction>>();
  }

  int32_t init_rpc_req_callback(uint64_t sequence, time_t timeout, rsp_callback_t callback);

  template <typename TAction>
  int register_action(const ::google::protobuf::ServiceDescriptor *service_desc, const std::string &rpc_name) {
    if (nullptr == service_desc) {
      return orbit::EN_ORBIT_ERROR_CODE_PARAM_ERROR;
    }
    std::string::size_type final_segment = rpc_name.find_last_of('.');
    std::string rpc_short_name;
    if (std::string::npos == final_segment) {
      rpc_short_name = rpc_name;
    } else {
      rpc_short_name = rpc_name.substr(final_segment + 1);
    }
    const ::google::protobuf::MethodDescriptor *method = service_desc->FindMethodByName(rpc_short_name);
    if (nullptr == method) {
      return orbit::EN_ORBIT_ERROR_CODE_METHOD_NOT_FOUND;
    }
    if (method->full_name() != rpc_name) {
      return orbit::EN_ORBIT_ERROR_CODE_RPC_NAME_ERROR;
    }
    return _register_action(static_cast<std::string>(method->full_name()), make_task_creator<TAction>());
  }

 public:
  int32_t send_rsp_to_proc(orbit::OrbitRpcMessage &orbit_msg);
  int32_t send_req_to_proc(orbit::OrbitRpcMessage &orbit_msg, uint64_t &sequence,
                           const OrbitClientRequestOptions &request_options = OrbitClientRequestOptions{});

  const std::string &get_empty_string() {
    static std::string ret;
    return ret;
  }

 private:
  int _register_action(const std::string &rpc_full_name, task_action_creator_t action);
  void on_create_task_failed(orbit::OrbitRpcMessage &orbit_msg, int32_t ret_code);
  void rsp_callback_execute();

  uint64_t sequence_allocator_;
  rpc_task_action_set_t task_action_map_by_name_;

  std::map<uint64_t, std::shared_ptr<rsp_callback_t>> sequence_callback_map_;
  std::multimap<time_t, std::pair<uint64_t, std::weak_ptr<rsp_callback_t>>> timeout_callback_map_;
};

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END