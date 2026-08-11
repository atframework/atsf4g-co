#include <Orbit/OrbitRPCReqBase.h>

#include <Orbit/OrbitClientRuntime.h>
#include <Orbit/OrbitRPCDispatcher.h>

#include <sstream>

ORBIT_CLIENT_SDK_NAMESPACE_BEGIN

namespace orbit_client_sdk {

task_action_orbit_req_base::task_action_orbit_req_base(msg_type&& msg) : rsp_init_(false), rsp_code_(0) {
  get_request().Swap(&msg);
}

task_action_orbit_req_base::~task_action_orbit_req_base() {}

task_action_orbit_req_base::msg_ref_type task_action_orbit_req_base::add_rsp_msg() {
  rsp_init_ = true;
  response_messages_.mutable_head()->set_error_code(rsp_code_);
  init_msg(response_messages_, get_request());
  return response_messages_;
}

int32_t task_action_orbit_req_base::init_msg(atfw::orbit::OrbitRpcMessage& msg, const atfw::orbit::OrbitRpcMessage& req_msg) {
  *msg.mutable_head() = req_msg.head();
  msg.mutable_head()->set_timestamp(::util::time::time_utility::get_sys_now());

  if (0 != req_msg.head().source_task_id()) {
    msg.mutable_head()->set_destination_task_id(req_msg.head().source_task_id());
  } else {
    msg.mutable_head()->set_destination_task_id(0);
  }

  if (0 != req_msg.head().destination_task_id()) {
    msg.mutable_head()->set_source_task_id(req_msg.head().destination_task_id());
  } else {
    msg.mutable_head()->set_source_task_id(0);
  }

  msg.mutable_head()->set_sequence(req_msg.head().sequence());
  return 0;
}

void task_action_orbit_req_base::send_response() {
  if (!rsp_init_) {
    return;
  }
  rsp_init_ = false;

  response_messages_.mutable_head()->set_error_code(rsp_code_);
  int32_t ret = OrbitRPCDispatcher::me()->send_rsp_to_proc(response_messages_);
  if (ret != 0) {
    std::ostringstream stream;
    stream << "send response to server failed, rsp head: "
           << OrbitClientRuntime::protobuf_mini_dumper_get_readable(response_messages_.head()) << ", ret: " << ret;
    OrbitClientRuntime::me()->log(OrbitClientLogLevel::kError, __FILE__, __LINE__, stream.str());
  }
}

}  // namespace orbit_client_sdk

ORBIT_CLIENT_SDK_NAMESPACE_END