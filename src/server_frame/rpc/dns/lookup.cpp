// Copyright 2023 atframework
// Created by owent on 2023/01/05.
//

#include "rpc/dns/lookup.h"

#include <memory>

#include "rpc/rpc_utils.h"

#include "dispatcher/ss_msg_dispatcher.h"

namespace rpc {
namespace dns {

rpc::result_code_type lookup(rpc::context& ctx, gsl::string_view domain, std::vector<address_record>& output) {
  task_manager::task_t* task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task", "rpc::dns::lookup");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  rpc::context child_ctx(ctx);
  rpc::context::tracer tracer;
  rpc::context::trace_option trace_option;
  trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  trace_option.is_remote = true;
  trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  child_ctx.setup_tracer(tracer, "rpc.dns.lookup", std::move(trace_option),
                         {{"rpc.system", "atrpc.ss"}, {"rpc.service", "rpc.dns"}, {"rpc.method", "rpc.dns.lookup"}});

  uint64_t sequence = ss_msg_dispatcher::me()->allocate_sequence();

  int32_t ret = ss_msg_dispatcher::me()->send_dns_lookup(domain, sequence, task->get_id());
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  void* received_raw_ptr = nullptr;
  ret = RPC_AWAIT_CODE_RESULT(
      rpc::custom_wait(ss_msg_dispatcher::me()->get_dns_lookup_rpc_type(), &received_raw_ptr, sequence));
  if (received_raw_ptr != nullptr) {
    output.swap(*reinterpret_cast<details::callback_data_type*>(received_raw_ptr));
  }

  RPC_RETURN_CODE(ret);
}

}  // namespace dns
}  // namespace rpc