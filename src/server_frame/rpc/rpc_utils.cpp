//
// Created by owent on 2016/10/4.
//

#include <log/log_wrapper.h>

#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>


#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>

#include "rpc_utils.h"


namespace rpc {
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
            }

            if (task->is_faulted()) {
                return hello::err::EN_SYS_RPC_TASK_KILLED;
            }

            if (task->is_canceled()) {
                return hello::err::EN_SYS_RPC_TASK_CANCELLED;
            }

            bool is_continue = true;
            for (int retry_times = 0; is_continue && retry_times < 5; ++ retry_times) {
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
                    WLOGERROR("task %llu resume and expect message type 0x%llx but real is 0x%llx", 
                        static_cast<unsigned long long>(task->get_id()),
                        static_cast<unsigned long long>(check_type),
                        static_cast<unsigned long long>(resume_data->message.msg_type)
                    );

                    is_continue = true;
                    continue;
                }

                if (0 != check_sequence && 0 != resume_data->sequence && check_sequence != resume_data->sequence) {
                    WLOGERROR("task %llu resume and expect message sequence %llu but real is %llu", 
                        static_cast<unsigned long long>(task->get_id()),
                        static_cast<unsigned long long>(check_sequence),
                        static_cast<unsigned long long>(resume_data->sequence)
                    );
                    is_continue = true;
                    continue;
                }

                msg.Swap(reinterpret_cast<TMSG *>(resume_data->message.msg_addr));
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

} // namespace rpc
