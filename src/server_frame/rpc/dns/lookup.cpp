// Copyright 2023 atframework
// Created by owent on 2023/01/05.
//

#include "rpc/dns/lookup.h"

#include <memory>

#include "config/logic_config.h"

#include "rpc/rpc_utils.h"

#include "dispatcher/ss_msg_dispatcher.h"

namespace rpc {
namespace dns {

rpc::result_code_type lookup(rpc::context& ctx, gsl::string_view domain, std::vector<address_record>& output) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task", "rpc::dns::lookup");

  rpc::context child_ctx(ctx);
  rpc::context::tracer tracer;
  rpc::context::trace_option trace_option;
  trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  trace_option.is_remote = true;
  trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  child_ctx.setup_tracer(tracer, "rpc.dns.lookup", std::move(trace_option),
                         {{"rpc.system", "atrpc.ss"}, {"rpc.service", "rpc.dns"}, {"rpc.method", "rpc.dns.lookup"}});

  uint64_t sequence = ss_msg_dispatcher::me()->allocate_sequence();

  int32_t ret = ss_msg_dispatcher::me()->send_dns_lookup(domain, sequence, ctx.get_task_context().task_id);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().dns().lookup_timeout(), std::chrono::seconds{5});

  dispatcher_resume_data_type result = dispatcher_make_default<dispatcher_resume_data_type>();
  ret = RPC_AWAIT_CODE_RESULT(
      rpc::custom_wait(ctx, ss_msg_dispatcher::me()->get_dns_lookup_rpc_type(), &result, await_options));
  if (result.message.msg_addr != nullptr) {
    output.swap(*reinterpret_cast<details::callback_data_type*>(result.message.msg_addr));
  }

  RPC_RETURN_CODE(ret);
}

}  // namespace dns
}  // namespace rpc
