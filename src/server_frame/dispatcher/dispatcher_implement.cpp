//
// Created by owt50 on 2016/9/26.
//

#include <typeinfo>

#include "actor_action_base.h"
#include "dispatcher_implement.h"


#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <utility/protobuf_mini_dumper.h>

#if defined(__GLIBCXX__) || defined(_LIBCOPP_ABI_VERSION)
#include <cxxabi.h>
#endif

int dispatcher_implement::init() { return 0; }

const char *dispatcher_implement::name() const {
    if (!human_readable_name_.empty()) {
        return human_readable_name_.c_str();
    }
#if defined(__GLIBCXX__) || defined(_LIBCOPP_ABI_VERSION)
    const char *raw_name = typeid(*this).name();
    int cxx_abi_status;
    char *readable_name = abi::__cxa_demangle(raw_name, 0, 0, &cxx_abi_status);
    if (NULL == readable_name) {
        human_readable_name_ = ::atapp::module_impl::name();
        return human_readable_name_.c_str();
    }

    human_readable_name_ = readable_name;
    free(readable_name);

#else
    human_readable_name_ = ::atapp::module_impl::name();
#endif

    return human_readable_name_.c_str();
}

uintptr_t dispatcher_implement::get_instance_ident() const { return reinterpret_cast<uintptr_t>(this); }

int32_t dispatcher_implement::on_recv_msg(msg_raw_t &msg, void *priv_data) {
    if (NULL == msg.msg_addr) {
        WLOGERROR("msg.msg_addr == NULL.");
        return hello::err::EN_SYS_PARAM;
    }

    if (get_instance_ident() != msg.msg_type) {
        WLOGERROR("msg.msg_type expected: 0x%llx, real: 0x%llx.", get_instance_ident_llu(), static_cast<unsigned long long>(msg.msg_type));
        return hello::err::EN_SYS_PARAM;
    }

    // 消息过滤器
    // 用于提供给所有消息进行前置处理的功能
    // 过滤器可以控制消息是否要下发下去
    if (!msg_filter_list_.empty()) {
        msg_filter_data_t filter_data;
        filter_data.msg = msg;

        for (std::list<msg_filter_handle_t>::iterator iter = msg_filter_list_.begin(); iter != msg_filter_list_.end(); ++iter) {
            if (false == (*iter)(filter_data)) {
                return 0;
            }
        }
    }

    uint64_t task_id = pick_msg_task_id(msg);
    if (task_id > 0) { // 如果是恢复任务则尝试切回协程任务
        resume_data_t callback_data;
        callback_data.message = msg;
        callback_data.private_data = priv_data;

        // 查找并恢复已有task
        return task_manager::me()->resume_task(task_id, callback_data);
    } else {
        start_data_t callback_data;
        callback_data.message = msg;
        callback_data.private_data = priv_data;

        // 先尝试使用task 模块
        int res = create_task(callback_data, task_id);

        if (res == hello::err::EN_SYS_NOTFOUND) {
            task_manager::actor_action_ptr_t actor;
            if (!actor_action_name_map_.empty()) {
                actor = create_actor(callback_data);
                // actor 流程
                if (actor) {
                    return actor->run();
                }
            }
        }

        if (res < 0) {
            WLOGERROR("%s(type=0x%llx) create task failed, errcode=%d", name(), get_instance_ident_llu(), res);
            return hello::err::EN_SYS_MALLOC;
        }

        // 不创建消息
        if (res == 0 && 0 == task_id) {
            return hello::err::EN_SUCCESS;
        }

        // 再启动
        return task_manager::me()->start_task(task_id, callback_data);
    }

    return 0;
}

int32_t dispatcher_implement::on_send_msg_failed(msg_raw_t &msg, int32_t error_code) {
    resume_data_t callback_data;
    callback_data.message = msg;
    callback_data.private_data = NULL;

    // msg->set_rpc_result(hello::err::EN_SYS_RPC_SEND_FAILED);
    uint64_t task_id = pick_msg_task_id(msg);
    if (task_id > 0) { // 如果是恢复任务则尝试切回协程任务
        WLOGERROR("dispatcher %s send data failed with error code = %d, try to resume task %llx", name(), error_code, static_cast<unsigned long long>(task_id));
        // 查找并恢复已有task
        return task_manager::me()->resume_task(task_id, callback_data);
    }

    WLOGERROR("send data failed with error code = %d", error_code);
    return 0;
}

int dispatcher_implement::create_task(start_data_t &start_data, task_manager::id_t &task_id) {
    task_id = 0;

    msg_type_t msg_type_id = pick_msg_type_id(start_data.message);
    if (0 == msg_type_id) {
        return hello::err::EN_SUCCESS;
    }

    if (task_action_name_map_.empty()) {
        return hello::err::EN_SYS_PARAM;
    }

    msg_task_action_set_t::iterator iter = task_action_name_map_.find(msg_type_id);
    if (task_action_name_map_.end() == iter) {
        return hello::err::EN_SYS_NOTFOUND;
    }

    return iter->second(task_id, start_data);
}

task_manager::actor_action_ptr_t dispatcher_implement::create_actor(start_data_t &start_data) {
    task_manager::actor_action_ptr_t ret;

    msg_type_t msg_type_id = pick_msg_type_id(start_data.message);
    if (0 == msg_type_id) {
        return ret;
    }

    if (actor_action_name_map_.empty()) {
        return ret;
    }

    msg_actor_action_set_t::iterator iter = actor_action_name_map_.find(msg_type_id);
    if (actor_action_name_map_.end() == iter) {
        return ret;
    }

    return iter->second(start_data);
}

void dispatcher_implement::push_filter_to_front(msg_filter_handle_t fn) { msg_filter_list_.push_front(fn); }

void dispatcher_implement::push_filter_to_back(msg_filter_handle_t fn) { msg_filter_list_.push_back(fn); }

const std::string &dispatcher_implement::get_empty_string() {
    static std::string ret;
    return ret;
}

int dispatcher_implement::_register_action(msg_type_t msg_type, task_manager::task_action_creator_t action) {
    msg_task_action_set_t::iterator iter = task_action_name_map_.find(msg_type);
    if (task_action_name_map_.end() != iter) {
        WLOGERROR("%s try to register more than one task actions to type %u.", name(), msg_type);
        return hello::err::EN_SYS_INIT;
    }

    task_action_name_map_[msg_type] = action;
    return hello::err::EN_SUCCESS;
}

int dispatcher_implement::_register_action(msg_type_t msg_type, task_manager::actor_action_creator_t action) {
    msg_actor_action_set_t::iterator iter = actor_action_name_map_.find(msg_type);
    if (actor_action_name_map_.end() != iter) {
        WLOGERROR("%s try to register more than one actor actions to type %u.", name(), msg_type);
        return hello::err::EN_SYS_INIT;
    }

    actor_action_name_map_[msg_type] = action;
    return hello::err::EN_SUCCESS;
}