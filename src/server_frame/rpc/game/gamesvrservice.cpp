/**
 * @brief Created by generate-for-pb.py for hello.GamesvrService, please don't edit it
 */

#include <log/log_wrapper.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>

#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <router/router_manager_set.h>
#include <router/router_manager_base.h>
#include <router/router_player_manager.h>
#include <router/router_object_base.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

#include "gamesvrservice.h"

namespace rpc {
    namespace details {
        template<class TBodyType>
        static inline int __pack_rpc_body(TBodyType &req_body, std::string *output, const char *rpc_full_name, const std::string &type_full_name) {
            if (false == req_body.SerializeToString(output)) {
                FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name, req_body.InitializationErrorString());
                return hello::err::EN_SYS_PACK;
            } else {
                FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name, protobuf_mini_dumper_get_readable(req_body));
                return hello::err::EN_SUCCESS;
            }
        }

        static inline void __setup_tracer(rpc::context &__child_ctx, rpc::context::tracer &__tracer, hello::SSMsgHead &head, const char *rpc_full_name) {
            __child_ctx.setup_tracer(__tracer, rpc_full_name);

            if (nullptr != __child_ctx.get_trace_span()) {
                auto trace_span = head.mutable_rpc_trace();
                if (nullptr != trace_span) {
                    protobuf_copy_message(*trace_span, *__child_ctx.get_trace_span());
                }
            }
        }

        static inline int __setup_rpc_request_header(hello::SSMsgHead &head, task_manager::task_t &task, const char *rpc_full_name,
                                                const std::string &type_full_name) {
            head.set_src_task_id(task.get_id());
            head.set_op_type(hello::EN_MSG_OP_TYPE_UNARY_REQUEST);
            atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
            if (nullptr == request_meta) {
                return hello::err::EN_SYS_MALLOC;
            }
            request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            request_meta->set_caller(ss_msg_dispatcher::me()->get_current_service_name());
            request_meta->set_callee("hello.GamesvrService");
            request_meta->set_rpc_name(rpc_full_name);
            request_meta->set_type_url(type_full_name);

            return hello::err::EN_SUCCESS;
        }
        template<class TResponseBody>
        static inline int __rpc_wait_and_unpack_response(rpc::context &__ctx, uint64_t rpc_sequence, TResponseBody &rsp_body, const char *rpc_full_name, 
                                                  const std::string &type_full_name) {
            hello::SSMsg* rsp_msg_ptr = __ctx.create<hello::SSMsg>();
            if (nullptr == rsp_msg_ptr) {
                FWLOGERROR("rpc {} create response message failed", rpc_full_name);
                return hello::err::EN_SYS_MALLOC;
            }

            hello::SSMsg& rsp_msg = *rsp_msg_ptr;
            int res = rpc::wait(rsp_msg, rpc_sequence);
            if (res < 0) {
                return res;
            }

            if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
                FWLOGERROR("rpc {} expect response message {}, but got {}", rpc_full_name,type_full_name, rsp_msg.head().rpc_response().type_url());
            }

            if (!rsp_msg.body_bin().empty()) {
                if (false == rsp_body.ParseFromString(rsp_msg.body_bin())) {
                    FWLOGERROR("rpc {} parse message {} for failed, msg: {}", rpc_full_name, type_full_name, rsp_body.InitializationErrorString());

                    return hello::err::EN_SYS_UNPACK;
                } else {
                    FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name, protobuf_mini_dumper_get_readable(rsp_body));
                }
            }

            return rsp_msg.head().error_code();
        }
    }

    namespace game {
        gamesvrservice_result_t::gamesvrservice_result_t() {}
        gamesvrservice_result_t::gamesvrservice_result_t(int code): result(code) {}
        gamesvrservice_result_t::operator int() const LIBCOPP_MACRO_NOEXCEPT {
            if (!result.is_ready()) {
                return 0;
            }

            const int* ret = result.data();
            if (nullptr == ret) {
                return 0;
            }

            return *ret;
        }

        bool gamesvrservice_result_t::is_success() const LIBCOPP_MACRO_NOEXCEPT {
            if (!result.is_ready()) {
                return false;
            }

            const int* ret = result.data();
            if (nullptr == ret) {
                return false;
            }

            return *ret >= 0;
        }

        bool gamesvrservice_result_t::is_error() const LIBCOPP_MACRO_NOEXCEPT {
            if (!result.is_ready()) {
                return false;
            }

            const int* ret = result.data();
            if (nullptr == ret) {
                return false;
            }

            return *ret < 0;
        }

        // ============ hello.GamesvrService.player_kickoff ============
        gamesvrservice_result_t player_kickoff(context& __ctx, uint64_t dst_bus_id, uint32_t zone_id, uint64_t user_id, const std::string& open_id, hello::SSPlayerKickOffReq &req_body, hello::SSPlayerKickOffRsp &rsp_body) {
            if (dst_bus_id == 0) {
                return gamesvrservice_result_t(hello::err::EN_SYS_PARAM);
            }

            task_manager::task_t *task = task_manager::task_t::this_task();
            if (!task) {
                FWLOGERROR("rpc {} must be called in a task", "hello.GamesvrService.player_kickoff");
                return gamesvrservice_result_t(hello::err::EN_SYS_RPC_NO_TASK);
            }

            hello::SSMsg* req_msg_ptr = __ctx.create<hello::SSMsg>();
            if (nullptr == req_msg_ptr) {
                FWLOGERROR("rpc {} create request message failed", "hello.GamesvrService.player_kickoff");
                return gamesvrservice_result_t(hello::err::EN_SYS_MALLOC);
            }

            int res;
            hello::SSMsg& req_msg = *req_msg_ptr;
            task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_self_bus_id());
            res = details::__setup_rpc_request_header(
                *req_msg.mutable_head(), *task, "hello.GamesvrService.player_kickoff",
                hello::SSPlayerKickOffReq::descriptor()->full_name()
            );
            if (res < 0) {
                return gamesvrservice_result_t(res);
            }

            res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(), "hello.GamesvrService.player_kickoff", 
                    hello::SSPlayerKickOffReq::descriptor()->full_name());
            if (res < 0) {
                return gamesvrservice_result_t(res);
            }

            rpc::context __child_ctx(__ctx);
            rpc::context::tracer __tracer;
            details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(), "hello.GamesvrService.player_kickoff");

            req_msg.mutable_head()->set_player_user_id(user_id);
            req_msg.mutable_head()->set_player_zone_id(zone_id);
            req_msg.mutable_head()->set_player_open_id(open_id);
            res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
            do {
                uint64_t rpc_sequence = req_msg.head().sequence();
                if (res < 0) {
                    break;
                }
                res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body, "hello.GamesvrService.player_kickoff",
                                                            hello::SSPlayerKickOffRsp::descriptor()->full_name());
            } while (false);

            if (res < 0) {
                FWLOGERROR("rpc {} call failed, res: {}({})", "hello.GamesvrService.player_kickoff",
                    res, protobuf_mini_dumper_get_error_msg(res)
                );
            }

            return gamesvrservice_result_t(__tracer.return_code(res));
        }
    } // namespace game
}