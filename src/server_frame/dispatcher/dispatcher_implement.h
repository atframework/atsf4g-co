//
// Created by owt50 on 2016/9/26.
//

#ifndef DISPATCHER_DISPATCHER_IMPLEMENT_H
#define DISPATCHER_DISPATCHER_IMPLEMENT_H

#pragma once

#include <list>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>


#include <atframe/atapp_module_impl.h>

#include <utility/environment_helper.h>
#include <utility/protobuf_mini_dumper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include "dispatcher_type_defines.h"

#include "task_manager.h"


class dispatcher_implement : public ::atapp::module_impl {
public:
    typedef dispatcher_msg_raw_t     msg_raw_t;
    typedef dispatcher_resume_data_t resume_data_t;
    typedef dispatcher_start_data_t  start_data_t;

    typedef uint32_t msg_type_t;
    typedef UTIL_ENV_AUTO_MAP(msg_type_t, task_manager::task_action_creator_t) msg_task_action_set_t;
    typedef UTIL_ENV_AUTO_MAP(msg_type_t, task_manager::actor_action_creator_t) msg_actor_action_set_t;

    struct msg_filter_data_t {
        msg_raw_t msg;
    };

    /**
     * @brief 消息过滤器函数，调用式为: bool(const msg_filter_data_t&)
     * @note 返回false可以中断后续过滤器的执行并禁止消息分发
     */
    typedef std::function<bool(const msg_filter_data_t &)> msg_filter_handle_t;

public:
    virtual int init() UTIL_CONFIG_OVERRIDE;

    virtual const char *name() const UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取实例标识，因为继承这个类型的都是单例，这个用来区分类型
     */
    uintptr_t get_instance_ident() const;

    unsigned long long get_instance_ident_llu() const { return static_cast<unsigned long long>(get_instance_ident()); }

    /**
     * @brief 接收消息回调接口，通常会尝试恢复协程任务运行或创建一个协程任务
     * @param msg 消息数据原始信息
     * @param msg_buf 数据地址
     * @param msg_size 数据长度
     * @return 返回错误码或0
     */
    virtual int32_t on_recv_msg(msg_raw_t &msg, void *priv_data);

    /**
     * @brief 发送消息消息失败的通知接口，通常会尝试填充错误码后恢复协程任务
     * @param msg 消息数据原始信息
     * @param msg_buf 数据地址
     * @param msg_size 数据长度
     * @param error_code 数据长度
     * @return 返回错误码或0
     */
    virtual int32_t on_send_msg_failed(msg_raw_t &msg, int32_t error_code);

    /**
     * @brief 数据解包
     * @param real_msg 实际的消息结构
     * @param raw_msg 消息抽象结构
     * @param msg_buf 数据地址
     * @param msg_size 数据长度
     * @return 返回错误码或0
     */
    template <typename TMsg>
    int32_t unpack_protobuf_msg(TMsg &real_msg, msg_raw_t &raw_msg, const void *msg_buf, size_t msg_size);

    template <typename TMsg>
    TMsg *get_protobuf_msg(msg_raw_t &raw_msg);

    /**
     * @brief 获取任务信息
     * @param raw_msg 消息抽象结构
     * @return 相关的任务id
     */
    virtual uint64_t pick_msg_task_id(msg_raw_t &raw_msg) = 0;

    /**
     * @brief 获取消息名称
     * @param raw_msg 消息抽象结构
     * @return 消息类型ID
     */
    virtual msg_type_t pick_msg_type_id(msg_raw_t &raw_msg) = 0;

    /**
     * @brief 创建协程任务
     * @param raw_msg 消息抽象结构
     * @param task_id 相关的任务id
     * @return 返回错误码或0
     */
    virtual int create_task(start_data_t &start_data, task_manager::id_t &task_id);

    /**
     * @brief 创建Actor
     * @param raw_msg 消息抽象结构
     * @return 返回错误码或0
     */
    virtual task_manager::actor_action_ptr_t create_actor(start_data_t &start_data);

    /**
     * @brief 注册Action
     * @param msg_name action名称
     * @return 或错误码
     */
    template <typename TAction>
    int register_action(msg_type_t msg_type) {
        return _register_action(msg_type, task_manager::me()->make_task_creator<TAction>());
    }

    /**
     * @brief 注册Action
     * @param msg_name action名称
     * @return 或错误码
     */
    template <typename TAction>
    int register_actor(msg_type_t msg_type) {
        return _register_action(msg_type, task_manager::me()->make_actor_creator<TAction>());
    }

    /**
     * @brief 添加前置过滤器
     * @param fn 函数或仿函数
     * @note 被添加的过滤器会先执行
     */
    void push_filter_to_front(msg_filter_handle_t fn);

    /**
     * @brief 添加后置过滤器
     * @param fn 函数或仿函数
     * @note 被添加的过滤器会最后执行
     */
    void push_filter_to_back(msg_filter_handle_t fn);

protected:
    const std::string &get_empty_string();

private:
    int _register_action(msg_type_t msg_type, task_manager::task_action_creator_t action);
    int _register_action(msg_type_t msg_type, task_manager::actor_action_creator_t action);

private:
    msg_task_action_set_t          task_action_name_map_;
    msg_actor_action_set_t         actor_action_name_map_;
    std::list<msg_filter_handle_t> msg_filter_list_;
    mutable std::string            human_readable_name_;
};


template <typename TMsg>
int32_t dispatcher_implement::unpack_protobuf_msg(TMsg &real_msg, msg_raw_t &raw_msg, const void *msg_buf, size_t msg_size) {
    raw_msg.msg_addr = NULL;
    raw_msg.msg_type = get_instance_ident();

    if (NULL == msg_buf || 0 == msg_size) {
        WLOGERROR("parameter error, NULL == msg_buf or 0 == msg_size");
        return hello::err::EN_SYS_PARAM;
    }

    if (false == real_msg.ParseFromArray(msg_buf, static_cast<int>(msg_size))) {
        WLOGERROR("unpack msg for %s(type=0x%llx) failed\n%s", name(), get_instance_ident_llu(), real_msg.InitializationErrorString().c_str());
        return hello::err::EN_SYS_UNPACK;
    }

    raw_msg.msg_addr = &real_msg;

    if (NULL != WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT) &&
        WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
        WLOGDEBUG("dispatcher %s(type=0x%llx) recv msg.\n%s", name(), get_instance_ident_llu(), protobuf_mini_dumper_get_readable(real_msg));
    }

    return 0;
}

template <typename TMsg>
TMsg *dispatcher_implement::get_protobuf_msg(msg_raw_t &raw_msg) {
    if (get_instance_ident() != raw_msg.msg_type) {
        return NULL;
    }

    return reinterpret_cast<TMsg *>(raw_msg.msg_addr);
}

#define REG_TASK_MSG_HANDLE(dispatcher, ret, act, proto)     \
    if (ret < 0) {                                           \
        dispatcher::me()->register_action<act>(proto);       \
    } else {                                                 \
        ret = dispatcher::me()->register_action<act>(proto); \
    }

#define REG_ACTOR_MSG_HANDLE(dispatcher, ret, act, proto)   \
    if (ret < 0) {                                          \
        dispatcher::me()->register_actor<act>(proto);       \
    } else {                                                \
        ret = dispatcher::me()->register_actor<act>(proto); \
    }

#endif // ATF4G_CO_DISPATCHER_IMPLEMENT_H
