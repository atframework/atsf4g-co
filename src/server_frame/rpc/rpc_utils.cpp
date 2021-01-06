//
// Created by owent on 2016/10/4.
//

#include <log/log_wrapper.h>

#include <std/smart_ptr.h>

#include <atframe/atapp.h>

#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>

#include <google/protobuf/util/time_util.h>

#include <config/compiler/protobuf_suffix.h>

#include "rpc_utils.h"
#include <rpc/db/uuid.h>

namespace rpc {

    namespace details {
        struct local_caller_info_t {
            uint64_t                        server_id;
            std::string                     server_id_string;
            std::string                     server_name;
            uint64_t                        server_type_id;
            std::string                     server_type_name;
            std::string                     app_version;
            std::string                     build_version;
            atapp::protocol::atapp_area     app_area;
            atapp::protocol::atapp_metadata app_metadata;
        };

        static local_caller_info_t g_context_caller_info;
    } // namespace details

    context::tracer::tracer() : return_code_(0), trace_span_(nullptr) {
        // TODO Call distributed tracing SDK API, zipkin for example
    }

    context::tracer::~tracer() {
        // TODO Call distributed tracing SDK API, zipkin for example
    }

    int context::tracer::return_code(int ret) {
        return_code_ = ret;
        return ret;
    }

    context::context() : trace_span_(nullptr) {}

    context::context(context &parent) : trace_span_(nullptr) {
        // reuse parent arena
        try_reuse_protobuf_arena(parent.mutable_protobuf_arena());

        // Set parent tracer data
        if (nullptr != parent.get_trace_span()) {
            set_trace_parent(*parent.get_trace_span());
        }
    }

    context::~context() {}

    void context::setup_tracer(tracer &tracer_instance, const char *name) {
        atframework::RpcTraceSpan *trace_span = mutable_trace_span();
        if (nullptr != trace_span && trace_span->span_id().empty()) {
            set_trace_span_id(rpc::db::uuid::generate_standard_uuid(true));
        }

        if (nullptr != trace_span && nullptr != name && trace_span->name().empty()) {
            trace_span->set_name(name);
        }

        // TODO Call distributed tracing SDK API, zipkin for example
        tracer_instance.start_timepoint_ = ::google::protobuf::util::TimeUtil::GetCurrentTime();
        tracer_instance.allocator_       = mutable_protobuf_arena();
        tracer_instance.trace_span_      = trace_span;
    }

    std::shared_ptr< ::google::protobuf::Arena> context::mutable_protobuf_arena() {
        if (allocator_) {
            return allocator_;
        }

        ::google::protobuf::ArenaOptions arena_options;
        arena_options.start_block_size = 512;   // 链路跟踪可能就占了200字节，起始可以大一点
        arena_options.max_block_size   = 65536; // 数据库的数据块比较大。最大值可以大一点

        allocator_ = std::make_shared< ::google::protobuf::Arena>(arena_options);
        return allocator_;
    }

    const std::shared_ptr< ::google::protobuf::Arena> &context::get_protobuf_arena() const { return allocator_; }

    bool context::try_reuse_protobuf_arena(const std::shared_ptr< ::google::protobuf::Arena> &arena) {
        if (!arena || allocator_) {
            return false;
        }

        allocator_ = arena;
        return true;
    }

    atframework::RpcTraceSpan *context::mutable_trace_span() {
        if (nullptr != trace_span_) {
            return trace_span_;
        }

        trace_span_ = create<atframework::RpcTraceSpan>();
        return trace_span_;
    }

    const atframework::RpcTraceSpan *context::get_trace_span() const { return trace_span_; }

    void context::set_trace_parent(const atframework::RpcTraceSpan &parent_span) {
        atframework::RpcTraceSpan *trace_span = mutable_trace_span();
        if (nullptr != trace_span) {
            trace_span->set_trace_id(parent_span.trace_id());
            trace_span->set_parent_id(parent_span.span_id());
        }
    }

    void context::set_trace_name(const std::string &name) {
        atframework::RpcTraceSpan *trace_span = mutable_trace_span();
        if (nullptr != trace_span) {
            trace_span->set_name(name);
        }
    }

    void context::set_trace_span_id(const std::string &span_id) {
        atframework::RpcTraceSpan *trace_span = mutable_trace_span();
        if (nullptr != trace_span) {
            trace_span->set_span_id(span_id);

            if (trace_span->trace_id().empty()) {
                trace_span->set_trace_id(span_id);
            }
        }
    }

    void context::set_trace_id(const std::string &trace_id) {
        atframework::RpcTraceSpan *trace_span = mutable_trace_span();
        if (nullptr != trace_span) {
            trace_span->set_trace_id(trace_id);
        }
    }

    void context::set_current_service(const atapp::app &app) {
        details::g_context_caller_info.server_id        = app.get_id();
        details::g_context_caller_info.server_id_string = app.convert_app_id_to_string(app.get_id());
        details::g_context_caller_info.server_name      = app.get_app_name();
        details::g_context_caller_info.server_type_id   = app.get_type_id();
        details::g_context_caller_info.server_type_name = app.get_type_name();
        details::g_context_caller_info.app_version      = app.get_app_version();
        details::g_context_caller_info.build_version    = app.get_build_version();
        protobuf_copy_message(details::g_context_caller_info.app_area, app.get_area());
        protobuf_copy_message(details::g_context_caller_info.app_metadata, app.get_metadata());
    }

    namespace detail {
        template <typename TMSG>
        static int wait(TMSG &msg, uintptr_t check_type, uint64_t check_sequence) {
            task_manager::task_t *task = task_manager::task_t::this_task();
            if (!task) {
                WLOGERROR("current not in a task");
                return hello::err::EN_SYS_RPC_NO_TASK;
            }

            if (task->is_timeout()) {
                return hello::err::EN_SYS_TIMEOUT;
            } else if (task->is_faulted()) {
                return hello::err::EN_SYS_RPC_TASK_KILLED;
            } else if (task->is_canceled()) {
                return hello::err::EN_SYS_RPC_TASK_CANCELLED;
            }

            bool is_continue = true;
            for (int retry_times = 0; is_continue && retry_times < 5; ++retry_times) {
                is_continue = false;
                // 协程 swap out
                void *result = NULL;
                task->yield(&result);

                dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

                // 协程 swap in

                if (task->is_timeout()) {
                    return hello::err::EN_SYS_TIMEOUT;
                }

                if (task->is_faulted()) {
                    return hello::err::EN_SYS_RPC_TASK_KILLED;
                }

                if (task->is_canceled()) {
                    return hello::err::EN_SYS_RPC_TASK_CANCELLED;
                }

                if (NULL == resume_data) {
                    WLOGERROR("task %llu resume data con not be empty", static_cast<unsigned long long>(task->get_id()));
                    return hello::err::EN_SYS_PARAM;
                }

                if (resume_data->message.msg_type != check_type) {
                    WLOGERROR("task %llu resume and expect message type 0x%llx but real is 0x%llx", static_cast<unsigned long long>(task->get_id()),
                              static_cast<unsigned long long>(check_type), static_cast<unsigned long long>(resume_data->message.msg_type));

                    is_continue = true;
                    continue;
                }

                if (0 != check_sequence && 0 != resume_data->sequence && check_sequence != resume_data->sequence) {
                    WLOGERROR("task %llu resume and expect message sequence %llu but real is %llu", static_cast<unsigned long long>(task->get_id()),
                              static_cast<unsigned long long>(check_sequence), static_cast<unsigned long long>(resume_data->sequence));
                    is_continue = true;
                    continue;
                }

                msg.Swap(reinterpret_cast<TMSG *>(resume_data->message.msg_addr));
            }

            return hello::err::EN_SUCCESS;
        }

        template <typename TMSG>
        static inline void wait_swap_message(std::shared_ptr<TMSG> &output, void *input) {
            if (output && input) {
                output->Swap(reinterpret_cast<TMSG *>(input));
            }
        }
        template <typename TMSG>
        static inline void wait_swap_message(std::unique_ptr<TMSG> &output, void *input) {
            if (output && input) {
                output->Swap(reinterpret_cast<TMSG *>(input));
            }
        }
        template <typename TMSG>
        static inline void wait_swap_message(TMSG *output, void *input) {
            if (output && input) {
                output->Swap(reinterpret_cast<TMSG *>(input));
            }
        }
        template <typename TMSG>
        static inline void wait_swap_message(TMSG &output, void *input) {
            if (input) {
                output.Swap(reinterpret_cast<TMSG *>(input));
            }
        }

        template <typename TMSG>
        static int wait(uintptr_t check_type, std::unordered_map<uint64_t, TMSG> &msg_waiters) {
            task_manager::task_t *task = task_manager::task_t::this_task();
            if (!task) {
                WLOGERROR("current not in a task");
                return hello::err::EN_SYS_RPC_NO_TASK;
            }

            if (task->is_timeout()) {
                return hello::err::EN_SYS_TIMEOUT;
            }

            if (task->is_faulted()) {
                return hello::err::EN_SYS_RPC_TASK_KILLED;
            }

            if (task->is_canceled()) {
                return hello::err::EN_SYS_RPC_TASK_CANCELLED;
            }

            std::unordered_set<uint64_t> received;
            received.reserve(msg_waiters.size());
            for (size_t retry_times = 0; received.size() < msg_waiters.size() && retry_times < msg_waiters.size() * 3; ++retry_times) {
                // 协程 swap out
                void *result = NULL;
                task->yield(&result);

                dispatcher_resume_data_t *resume_data = reinterpret_cast<dispatcher_resume_data_t *>(result);

                // 协程 swap in

                if (task->is_timeout()) {
                    return hello::err::EN_SYS_TIMEOUT;
                }

                if (task->is_faulted()) {
                    return hello::err::EN_SYS_RPC_TASK_KILLED;
                }

                if (task->is_canceled()) {
                    return hello::err::EN_SYS_RPC_TASK_CANCELLED;
                }

                if (NULL == resume_data) {
                    WLOGERROR("task %llu resume data con not be empty", static_cast<unsigned long long>(task->get_id()));
                    return hello::err::EN_SYS_PARAM;
                }

                if (resume_data->message.msg_type != check_type) {
                    WLOGERROR("task %llu resume and expect message type 0x%llx but real is 0x%llx", static_cast<unsigned long long>(task->get_id()),
                              static_cast<unsigned long long>(check_type), static_cast<unsigned long long>(resume_data->message.msg_type));

                    continue;
                }

                auto rsp_iter = msg_waiters.find(resume_data->sequence);
                if (rsp_iter == msg_waiters.end()) {
                    WLOGERROR("task %llu resume and with message sequence %llu but not found in waiters", static_cast<unsigned long long>(task->get_id()),
                              static_cast<unsigned long long>(resume_data->sequence));
                    continue;
                }

                wait_swap_message(rsp_iter->second, resume_data->message.msg_addr);
                received.insert(resume_data->sequence);
            }

            return hello::err::EN_SUCCESS;
        }
    } // namespace detail

    int wait(hello::SSMsg &msg, uint64_t check_sequence) {
        int ret = detail::wait(msg, ss_msg_dispatcher::me()->get_instance_ident(), check_sequence);
        if (0 != ret) {
            return ret;
        }

        return msg.head().error_code();
    }

    int wait(hello::table_all_message &msg, uint64_t check_sequence) {
        int ret = detail::wait(msg, db_msg_dispatcher::me()->get_instance_ident(), check_sequence);
        if (0 != ret) {
            return ret;
        }

        return msg.error_code();
    }

    int wait(std::unordered_map<uint64_t, hello::SSMsg> &msg_waiters) { return detail::wait(ss_msg_dispatcher::me()->get_instance_ident(), msg_waiters); }

    int wait(std::unordered_map<uint64_t, hello::SSMsg *> &msg_waiters) { return detail::wait(ss_msg_dispatcher::me()->get_instance_ident(), msg_waiters); }

} // namespace rpc
