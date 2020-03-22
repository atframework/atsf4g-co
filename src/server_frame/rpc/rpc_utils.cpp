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
        static int wait(TMSG &msg, uintptr_t check_type) {
            task_manager::task_t *task = task_manager::task_t::this_task();
            if (!task) {
                WLOGERROR("current not in a task");
                return hello::err::EN_SYS_RPC_NO_TASK;
            }

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

            if (NULL != resume_data) {
                if (resume_data->message.msg_type == check_type) {
                    msg.Swap(reinterpret_cast<TMSG *>(resume_data->message.msg_addr));
                } else {
                    WLOGERROR("expect msg type 0x%llx but real is 0x%llx", static_cast<unsigned long long>(check_type),
                              static_cast<unsigned long long>(resume_data->message.msg_type));
                }
            }

            return hello::err::EN_SUCCESS;
        }
    } // namespace detail

    int wait(hello::SSMsg &msg) {
        int ret = detail::wait(msg, ss_msg_dispatcher::me()->get_instance_ident());
        if (0 != ret) {
            return ret;
        }

        return msg.head().error_code();
    }

    int wait(hello::table_all_message &msg) {
        int ret = detail::wait(msg, db_msg_dispatcher::me()->get_instance_ident());
        if (0 != ret) {
            return ret;
        }

        return msg.error_code();
    }

} // namespace rpc
