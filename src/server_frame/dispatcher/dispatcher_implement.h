//
// Created by owt50 on 2016/9/26.
//

#ifndef DISPATCHER_DISPATCHER_IMPLEMENT_H
#define DISPATCHER_DISPATCHER_IMPLEMENT_H

#pragma once

#include <list>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>

#include <atframe/atapp.h>
#include <atframe/atapp_module_impl.h>
#include <libatbus_protocol.h>

#include <utility/environment_helper.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include "dispatcher_type_defines.h"

#include "task_manager.h"


class dispatcher_implement : public ::atapp::module_impl {
public:
    typedef hello::EnMsgOpType       msg_op_type_t;
    typedef dispatcher_msg_raw_t     msg_raw_t;
    typedef dispatcher_resume_data_t resume_data_t;
    typedef dispatcher_start_data_t  start_data_t;

    typedef uint32_t msg_type_t;
    typedef UTIL_ENV_AUTO_MAP(msg_type_t, task_manager::task_action_creator_t) msg_task_action_set_t;
    typedef UTIL_ENV_AUTO_MAP(msg_type_t, task_manager::actor_action_creator_t) msg_actor_action_set_t;

    typedef UTIL_ENV_AUTO_MAP(std::string, task_manager::task_action_creator_t) rpc_task_action_set_t;
    typedef UTIL_ENV_AUTO_MAP(std::string, task_manager::actor_action_creator_t) rpc_actor_action_set_t;

    struct msg_filter_data_t {
        dispatcher_msg_raw_t msg;

        inline msg_filter_data_t(const dispatcher_msg_raw_t &m) : msg(m) {}
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
    virtual int32_t on_receive_message(rpc::context &ctx, msg_raw_t &msg, void *priv_data, uint64_t sequence);

    /**
     * @brief 发送消息消息失败的通知接口，通常会尝试填充错误码后恢复协程任务
     * @param msg 消息数据原始信息
     * @param msg_buf 数据地址
     * @param msg_size 数据长度
     * @param error_code 数据长度
     * @return 返回错误码或0
     */
    virtual int32_t on_send_message_failed(rpc::context &ctx, msg_raw_t &msg, int32_t error_code, uint64_t sequence);

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
     * @brief 获取消息类型ID
     * @param raw_msg 消息抽象结构
     * @return 消息类型ID
     */
    virtual msg_type_t pick_msg_type_id(msg_raw_t &raw_msg) = 0;

    /**
     * @brief 获取消息的RPC名字
     * @param raw_msg 消息抽象结构
     * @return 消息的RPC名字,如果不是RPC消息，返回空字符串
     */
    virtual const std::string &pick_rpc_name(msg_raw_t &raw_msg) = 0;

    /**
     * @brief 获取操作类型
     * @param raw_msg 消息抽象结构
     * @note 这只是一个调度曾规范，不强制执行。详情 @see hello::EnMsgOpType
     * @return 消息操作类型
     */
    virtual msg_op_type_t pick_msg_op_type(msg_raw_t &raw_msg) = 0;

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
     * @brief 根据类型ID获取action或actor选项
     * @param raw_msg 消息抽象结构
     * @return 返回action或actor选项或NULL
     */
    virtual const atframework::DispatcherOptions *get_options_by_message_type(msg_type_t msg_type);

    /**
     * @brief 注册Action
     * @param msg_type 消息类型ID
     * @return 或错误码
     */
    template <typename TAction>
    inline int register_action(msg_type_t msg_type) {
        const atframework::DispatcherOptions *options = get_options_by_message_type(msg_type);
        if (nullptr != options) {
            return _register_action(msg_type, task_manager::me()->make_task_creator<TAction>(options));
        } else {
            return _register_action(msg_type, task_manager::me()->make_task_creator<TAction>(options));
        }
    }

    /**
     * @brief 注册Action
     * @param rpc_full_name 注册的RPC完整名称
     * @return 或错误码
     */
    template <typename TAction>
    int register_action(const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::ServiceDescriptor *service_desc, const std::string &rpc_name) {
        if (nullptr == service_desc) {
            return hello::err::EN_SYS_PARAM;
        }

        std::string::size_type final_segment = rpc_name.find_last_of('.');
        std::string            rpc_short_name;
        if (std::string::npos == final_segment) {
            rpc_short_name = rpc_name;
        } else {
            rpc_short_name = rpc_name.substr(final_segment + 1);
        }
        const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = service_desc->FindMethodByName(rpc_short_name);
        if (nullptr == method) {
            FWLOGERROR("{} try to register rpc action {} for service {} failed, not found", name(), rpc_name, service_desc->full_name());
            return hello::err::EN_SYS_NOTFOUND;
        }

        if (method->full_name() != rpc_name) {
            FWLOGERROR("{} try to register rpc action {} for service {} failed, the real full name is {}", name(), rpc_name, service_desc->full_name(),
                       method->full_name());
            return hello::err::EN_SYS_NOTFOUND;
        }

        const atframework::DispatcherOptions *options = nullptr;
        if (method->options().HasExtension(atframework::rpc_options)) {
            options = &method->options().GetExtension(atframework::rpc_options);
        }

        return _register_action(method->full_name(), task_manager::me()->make_task_creator<TAction>(options));
    }

    /**
     * @brief 注册Action
     * @param msg_type 消息类型ID
     * @return 或错误码
     */
    template <typename TAction>
    inline int register_actor(msg_type_t msg_type) {
        const atframework::DispatcherOptions *options = get_options_by_message_type(msg_type);
        return _register_action(msg_type, task_manager::me()->make_actor_creator<TAction>(options));
    }

    /**
     * @brief 注册Action
     * @param rpc_full_name 注册的RPC完整名称
     * @return 或错误码
     */
    template <typename TAction>
    int register_actor(const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::ServiceDescriptor *service_desc, const std::string &rpc_name) {
        if (nullptr == service_desc) {
            return hello::err::EN_SYS_PARAM;
        }

        std::string::size_type final_segment = rpc_name.find_last_of('.');
        std::string            rpc_short_name;
        if (std::string::npos == final_segment) {
            rpc_short_name = rpc_name;
        } else {
            rpc_short_name = rpc_name.substr(final_segment + 1);
        }
        const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method = service_desc->FindMethodByName(rpc_short_name);
        if (nullptr == method) {
            FWLOGERROR("{} try to register rpc actor {} for service {} failed, not found", name(), rpc_name, service_desc->full_name());
            return hello::err::EN_SYS_NOTFOUND;
        }

        if (method->full_name() != rpc_name) {
            FWLOGERROR("{} try to register rpc action {} for service {} failed, the real full name is {}", name(), rpc_name, service_desc->full_name(),
                       method->full_name());
            return hello::err::EN_SYS_NOTFOUND;
        }

        const atframework::DispatcherOptions *options = nullptr;
        if (method->options().HasExtension(atframework::rpc_options)) {
            options = &method->options().GetExtension(atframework::rpc_options);
        }

        return _register_action(method->full_name(), task_manager::me()->make_actor_creator<TAction>(options));
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
    int _register_action(const std::string &rpc_full_name, task_manager::task_action_creator_t action);
    int _register_action(const std::string &rpc_full_name, task_manager::actor_action_creator_t action);

private:
    msg_task_action_set_t          task_action_map_by_id_;
    msg_actor_action_set_t         actor_action_map_by_id_;
    rpc_task_action_set_t          task_action_map_by_name_;
    rpc_actor_action_set_t         actor_action_map_by_name_;
    std::list<msg_filter_handle_t> msg_filter_list_;
    mutable std::string            human_readable_name_;
};


template <typename TMsg>
int32_t dispatcher_implement::unpack_protobuf_msg(TMsg &real_msg, msg_raw_t &raw_msg, const void *msg_buf, size_t msg_size) {
    raw_msg.msg_addr = NULL;
    raw_msg.msg_type = get_instance_ident();

    if (NULL == msg_buf || 0 == msg_size) {
        FWLOGERROR("{} try to parameter error, NULL == msg_buf or 0 == msg_size", name());
        return hello::err::EN_SYS_PARAM;
    }

    if (false == real_msg.ParseFromArray(msg_buf, static_cast<int>(msg_size))) {
        FWLOGERROR("{} unpack msg for type={} failed\n{}", name(), get_instance_ident_llu(), real_msg.InitializationErrorString().c_str());
        return hello::err::EN_SYS_UNPACK;
    }

    raw_msg.msg_addr = &real_msg;

    if (NULL != WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT) &&
        WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
        FWLOGDEBUG("{}(type={}) recv msg.\n{}", name(), get_instance_ident_llu(), protobuf_mini_dumper_get_readable(real_msg));
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

#define REG_TASK_RPC_HANDLE(dispatcher, ret, act, service_desc, rpc_name)     \
    if (ret < 0) {                                                            \
        dispatcher::me()->register_action<act>(service_desc, rpc_name);       \
    } else {                                                                  \
        ret = dispatcher::me()->register_action<act>(service_desc, rpc_name); \
    }

#define REG_ACTOR_RPC_HANDLE(dispatcher, ret, act, service_desc, rpc_name)   \
    if (ret < 0) {                                                           \
        dispatcher::me()->register_actor<act>(service_desc, rpc_name);       \
    } else {                                                                 \
        ret = dispatcher::me()->register_actor<act>(service_desc, rpc_name); \
    }


#endif // ATF4G_CO_DISPATCHER_IMPLEMENT_H
