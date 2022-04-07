// Copyright 2022 atframework
// @brief Created by generate-for-pb.py for atframework.distributed_system.DtcoordsvrService, please don't edit it

#include "dtcoordsvrservice.h"

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <router/router_manager_set.h>
#include <router/router_manager_base.h>
#include <router/router_player_manager.h>
#include <router/router_object_base.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
namespace details {
template<class TBodyType>
static inline int __pack_rpc_body(TBodyType &&input, std::string *output, gsl::string_view rpc_full_name,
                                const std::string &type_full_name) {
  if (false == input.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
              input.InitializationErrorString());
    return hello::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
              protobuf_mini_dumper_get_readable(input));
    return hello::err::EN_SUCCESS;
  }
}

template<class TBodyType>
static inline int __unpack_rpc_body(TBodyType &&output, const std::string& input, gsl::string_view rpc_full_name,
                                const std::string &type_full_name) {
  if (false == output.ParseFromString(input)) {
    FWLOGERROR("rpc {} parse message {} failed, msg: {}", rpc_full_name, type_full_name,
              output.InitializationErrorString());
    return hello::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
              protobuf_mini_dumper_get_readable(output));
    return hello::err::EN_SUCCESS;
  }
}

static inline rpc::context::tracer::span_ptr_type __setup_tracer(rpc::context &__child_ctx,
                                  rpc::context::tracer &__tracer,
                                  atframework::SSMsgHead &head, gsl::string_view rpc_full_name,
                                  gsl::string_view service_full_name) {
  rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/semantic_conventions/README.md
  __child_ctx.setup_tracer(__tracer, rpc::context::string_view{rpc_full_name.data(), rpc_full_name.size()},
    std::move(__trace_option), {
    {"rpc.system", "atrpc.ss"},
    {"rpc.service", rpc::context::string_view{service_full_name.data(), service_full_name.size()}},
    {"rpc.method", rpc::context::string_view{rpc_full_name.data(), rpc_full_name.size()}}
  });
  rpc::context::tracer::span_ptr_type __child_trace_span = __child_ctx.get_trace_span();
  if (__child_trace_span) {
    auto trace_span_head = head.mutable_rpc_trace();
    if (trace_span_head) {
      auto trace_context = __child_trace_span->GetContext();
      rpc::context::tracer::trace_id_span trace_id = trace_context.trace_id().Id();
      rpc::context::tracer::span_id_span span_id = trace_context.span_id().Id();

      trace_span_head->mutable_trace_id()->assign(reinterpret_cast<const char *>(trace_id.data()), trace_id.size());
      trace_span_head->mutable_span_id()->assign(reinterpret_cast<const char *>(span_id.data()), span_id.size());
      trace_span_head->set_kind(__trace_option.kind);
      trace_span_head->set_name(static_cast<std::string>(rpc_full_name));

      // trace_context.IsSampled();
    }
  }
  return __child_trace_span;
}

static inline int __setup_rpc_stream_header(atframework::SSMsgHead &head, gsl::string_view rpc_full_name,
                                            const std::string &type_full_name) {
  head.set_op_type(hello::EN_MSG_OP_TYPE_STREAM);
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return hello::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("atframework.distributed_system.DtcoordsvrService");
  stream_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  stream_meta->set_type_url(type_full_name);

  return hello::err::EN_SUCCESS;
}
static inline int __setup_rpc_request_header(atframework::SSMsgHead &head, task_manager::task_t &task,
                                             gsl::string_view rpc_full_name,
                                             const std::string &type_full_name) {
  head.set_src_task_id(task.get_id());
  head.set_op_type(hello::EN_MSG_OP_TYPE_UNARY_REQUEST);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return hello::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee("atframework.distributed_system.DtcoordsvrService");
  request_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  request_meta->set_type_url(type_full_name);

  return hello::err::EN_SUCCESS;
}
template<class TCode, class TConvertList>
static inline bool __redirect_rpc_result_to_info_log(TCode &origin_result, TConvertList&& convert_list, 
                                        gsl::string_view rpc_full_name, const std::string &type_full_name) {
  for (auto& check: convert_list) {
    if (origin_result == check) {
      FWLOGINFO("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name,
                origin_result, protobuf_mini_dumper_get_error_msg(origin_result)
      );

      return true;
    }
  }

  return false;
}
template<class TCode, class TConvertList>
static inline bool __redirect_rpc_result_to_warning_log(TCode &origin_result, TConvertList&& convert_list, 
                                        gsl::string_view rpc_full_name, const std::string &type_full_name) {
  for (auto& check: convert_list) {
    if (origin_result == check) {
      FWLOGWARNING("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name,
                   origin_result, protobuf_mini_dumper_get_error_msg(origin_result)
      );

      return true;
    }
  }

  return false;
}
template<class TResponseBody>
static inline int __rpc_wait_and_unpack_response(rpc::context &__ctx, uint64_t rpc_sequence, TResponseBody &rsp_body,
                                            gsl::string_view rpc_full_name, const std::string &type_full_name) {
  atframework::SSMsg* rsp_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FWLOGERROR("rpc {} create response message failed", rpc_full_name);
    return hello::err::EN_SYS_MALLOC;
  }

  atframework::SSMsg& rsp_msg = *rsp_msg_ptr;
  int res = RPC_AWAIT_CODE_RESULT(rpc::wait(rsp_msg, rpc_sequence));
  if (res < 0) {
    return res;
  }

  if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
    FWLOGERROR("rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
               rsp_msg.head().rpc_response().type_url());
  }

  if (!rsp_msg.body_bin().empty()) {
    return details::__unpack_rpc_body(rsp_body, rsp_msg.body_bin(), rpc_full_name, type_full_name);
  }

  return rsp_msg.head().error_code();
}
}  // namespace details

namespace transaction {

// ============ atframework.distributed_system.DtcoordsvrService.query ============
namespace packer {
bool pack_query(std::string& output, const atframework::distributed_system::SSDistributeTransactionQueryReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.query", 
                                 atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name());
}

bool unpack_query(const std::string& input, atframework::distributed_system::SSDistributeTransactionQueryReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.query", 
                                 atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name());
}

bool pack_query(std::string& output, const atframework::distributed_system::SSDistributeTransactionQueryRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.query", 
                                 atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name());
}

bool unpack_query(const std::string& input, atframework::distributed_system::SSDistributeTransactionQueryRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.query", 
                                 atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type query(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionQueryReq &req_body, atframework::distributed_system::SSDistributeTransactionQueryRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.query");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.query");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.query", 
      atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.query",
      atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.query", 
                                 atframework::distributed_system::SSDistributeTransactionQueryReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.query",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.query",
        atframework::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
    const int warning_codes[] = {PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "atframework.distributed_system.DtcoordsvrService.query",
        atframework::distributed_system::SSDistributeTransactionQueryRsp::descriptor()->full_name())) {
      RPC_RETURN_CODE(__tracer.return_code(res));
    }
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.query",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}

// ============ atframework.distributed_system.DtcoordsvrService.create ============
namespace packer {
bool pack_create(std::string& output, const atframework::distributed_system::SSDistributeTransactionCreateReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.create", 
                                 atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name());
}

bool unpack_create(const std::string& input, atframework::distributed_system::SSDistributeTransactionCreateReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.create", 
                                 atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name());
}

bool pack_create(std::string& output, const atframework::distributed_system::SSDistributeTransactionCreateRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.create", 
                                 atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name());
}

bool unpack_create(const std::string& input, atframework::distributed_system::SSDistributeTransactionCreateRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.create", 
                                 atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type create(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionCreateReq &req_body, atframework::distributed_system::SSDistributeTransactionCreateRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.create");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.create");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.create", 
      atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.create",
      atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.create", 
                                 atframework::distributed_system::SSDistributeTransactionCreateReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.create",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.create",
        atframework::distributed_system::SSDistributeTransactionCreateRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.create",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}

// ============ atframework.distributed_system.DtcoordsvrService.commit ============
namespace packer {
bool pack_commit(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.commit", 
                                 atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name());
}

bool unpack_commit(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.commit", 
                                 atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name());
}

bool pack_commit(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.commit", 
                                 atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name());
}

bool unpack_commit(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.commit", 
                                 atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type commit(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionCommitReq &req_body, atframework::distributed_system::SSDistributeTransactionCommitRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.commit");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.commit");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.commit", 
      atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.commit",
      atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.commit", 
                                 atframework::distributed_system::SSDistributeTransactionCommitReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.commit",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.commit",
        atframework::distributed_system::SSDistributeTransactionCommitRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
    const int warning_codes[] = {PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "atframework.distributed_system.DtcoordsvrService.commit",
        atframework::distributed_system::SSDistributeTransactionCommitRsp::descriptor()->full_name())) {
      RPC_RETURN_CODE(__tracer.return_code(res));
    }
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.commit",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}

// ============ atframework.distributed_system.DtcoordsvrService.reject ============
namespace packer {
bool pack_reject(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.reject", 
                                 atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name());
}

bool unpack_reject(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.reject", 
                                 atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name());
}

bool pack_reject(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.reject", 
                                 atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name());
}

bool unpack_reject(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.reject", 
                                 atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type reject(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionRejectReq &req_body, atframework::distributed_system::SSDistributeTransactionRejectRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.reject");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.reject");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.reject", 
      atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.reject",
      atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.reject", 
                                 atframework::distributed_system::SSDistributeTransactionRejectReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.reject",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.reject",
        atframework::distributed_system::SSDistributeTransactionRejectRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
    const int warning_codes[] = {PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "atframework.distributed_system.DtcoordsvrService.reject",
        atframework::distributed_system::SSDistributeTransactionRejectRsp::descriptor()->full_name())) {
      RPC_RETURN_CODE(__tracer.return_code(res));
    }
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.reject",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}

// ============ atframework.distributed_system.DtcoordsvrService.commit_participator ============
namespace packer {
bool pack_commit_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.commit_participator", 
                                 atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name());
}

bool unpack_commit_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.commit_participator", 
                                 atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name());
}

bool pack_commit_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.commit_participator", 
                                 atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name());
}

bool unpack_commit_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.commit_participator", 
                                 atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type commit_participator(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq &req_body, atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.commit_participator");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.commit_participator");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.commit_participator", 
      atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.commit_participator",
      atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.commit_participator", 
                                 atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.commit_participator",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.commit_participator",
        atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
    const int warning_codes[] = {PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "atframework.distributed_system.DtcoordsvrService.commit_participator",
        atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp::descriptor()->full_name())) {
      RPC_RETURN_CODE(__tracer.return_code(res));
    }
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.commit_participator",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}

// ============ atframework.distributed_system.DtcoordsvrService.reject_participator ============
namespace packer {
bool pack_reject_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.reject_participator", 
                                 atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name());
}

bool unpack_reject_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.reject_participator", 
                                 atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name());
}

bool pack_reject_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.reject_participator", 
                                 atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name());
}

bool unpack_reject_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.reject_participator", 
                                 atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type reject_participator(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq &req_body, atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.reject_participator");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.reject_participator");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.reject_participator", 
      atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.reject_participator",
      atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.reject_participator", 
                                 atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.reject_participator",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.reject_participator",
        atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
    const int warning_codes[] = {PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "atframework.distributed_system.DtcoordsvrService.reject_participator",
        atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp::descriptor()->full_name())) {
      RPC_RETURN_CODE(__tracer.return_code(res));
    }
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.reject_participator",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}

// ============ atframework.distributed_system.DtcoordsvrService.remove ============
namespace packer {
bool pack_remove(std::string& output, const atframework::distributed_system::SSDistributeTransactionRemoveReq& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.remove", 
                                 atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name());
}

bool unpack_remove(const std::string& input, atframework::distributed_system::SSDistributeTransactionRemoveReq& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.remove", 
                                 atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name());
}

bool pack_remove(std::string& output, const atframework::distributed_system::SSDistributeTransactionRemoveRsp& input) {
  return hello::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "atframework.distributed_system.DtcoordsvrService.remove", 
                                 atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name());
}

bool unpack_remove(const std::string& input, atframework::distributed_system::SSDistributeTransactionRemoveRsp& output) {
  return hello::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "atframework.distributed_system.DtcoordsvrService.remove", 
                                 atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name());
}

}  // namespace packer

rpc::result_code_type remove(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionRemoveReq &req_body, atframework::distributed_system::SSDistributeTransactionRemoveRsp &rsp_body, bool __no_wait, uint64_t* __wait_later) {
  if (dst_bus_id == 0) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "atframework.distributed_system.DtcoordsvrService.remove");
    RPC_RETURN_CODE(hello::err::EN_SYS_RPC_NO_TASK);
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "atframework.distributed_system.DtcoordsvrService.remove");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
  if (__no_wait) {
    res = details::__setup_rpc_stream_header(
      *req_msg.mutable_head(), "atframework.distributed_system.DtcoordsvrService.remove", 
      atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name()
    );
  } else {
    res = details::__setup_rpc_request_header(
      *req_msg.mutable_head(), *task, "atframework.distributed_system.DtcoordsvrService.remove",
      atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name()
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "atframework.distributed_system.DtcoordsvrService.remove", 
                                 atframework::distributed_system::SSDistributeTransactionRemoveReq::descriptor()->full_name());
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "atframework.distributed_system.DtcoordsvrService.remove",
                          "atframework.distributed_system.DtcoordsvrService");

  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "atframework.distributed_system.DtcoordsvrService.remove",
        atframework::distributed_system::SSDistributeTransactionRemoveRsp::descriptor()->full_name());
  } while (false);

  if (res < 0) {
    const int warning_codes[] = {PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "atframework.distributed_system.DtcoordsvrService.remove",
        atframework::distributed_system::SSDistributeTransactionRemoveRsp::descriptor()->full_name())) {
      RPC_RETURN_CODE(__tracer.return_code(res));
    }
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "atframework.distributed_system.DtcoordsvrService.remove",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.return_code(res));
}
}  // namespace transaction
}