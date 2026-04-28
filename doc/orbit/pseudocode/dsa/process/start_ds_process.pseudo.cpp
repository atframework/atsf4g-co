#include "start_ds_process.pseudo.h"

#include "../agent/local_channel_service.pseudo.h"

namespace atorbit {
namespace dsa {
namespace process {

namespace {

static unsigned long long clamp_launch_arg_count(unsigned long long custom_arg_count, unsigned long long capacity) {
  if (0 == capacity) {
    return 0;
  }

  if (custom_arg_count + 1 < capacity) {
    return custom_arg_count + 1;
  }

  return capacity;
}

}  // namespace

start_ds_process::start_ds_process(shared::runtime::resource_ledger* ledger,
                                   agent::local_channel_service* local_channel_service)
    : ledger_(ledger), local_channel_service_(local_channel_service) {}

start_ds_result_t start_ds_process::launch(const start_ds_request_t& request) {
  start_ds_result_t result;
  if (nullptr == ledger_ || nullptr == local_channel_service_ || 0 == request.ds_id) {
    result.status = start_ds_status_t::k_invalid_argument;
    return result;
  }

  const char* launch_args[16] = {};
  auto launch_arg_count = build_launch_args(request, launch_args, 16);
  result.process_id = start_process(request, launch_args, launch_arg_count);
  if (result.process_id <= 0) {
    result.status = start_ds_status_t::k_launch_failed;
    rollback_failed_launch(request.ds_id);
    return result;
  }

  result.local_endpoint = create_local_endpoint(request.ds_id);
  auto bind_result = local_channel_service_->register_ds(request.ds_id, result.local_endpoint, result.process_id);
  if (bind_result != 0) {
    result.status = start_ds_status_t::k_channel_failed;
    rollback_failed_launch(request.ds_id);
    return result;
  }

  ledger_->commit_running(request.ds_id);
  result.status = start_ds_status_t::k_ok;
  return result;
}

void start_ds_process::rollback_failed_launch(unsigned long long ds_id) {
  if (nullptr != ledger_) {
    ledger_->release(ds_id);
  }

  if (nullptr != local_channel_service_) {
    local_channel_service_->remove_ds(ds_id);
  }
}

int start_ds_process::start_process(const start_ds_request_t& request,
                                    const char* const launch_args[],
                                    unsigned long long launch_arg_count) {
  // 真实代码阶段会把 launch_args 传入进程启动器。
  (void)request;
  (void)launch_args;
  (void)launch_arg_count;
  return 4321;
}

const char* start_ds_process::create_local_endpoint(unsigned long long ds_id) {
  (void)ds_id;
  return "local://dsa_ds_channel";
}

unsigned long long start_ds_process::build_launch_args(const start_ds_request_t& request,
                                                       const char* output_args[],
                                                       unsigned long long capacity) const {
  // 真实代码阶段这里会写入 output_args。
  // 当前伪代码只保留参数数量规则，避免把 transport / argv 细节展开到实现前阶段。
  (void)output_args;
  return clamp_launch_arg_count(request.custom_arg_count, capacity);
}

}  // namespace process
}  // namespace dsa
}  // namespace atorbit
