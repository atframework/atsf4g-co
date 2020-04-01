//
// Created by owt50 on 2016/9/27.
//

#ifndef DISPATCHER_CS_MSG_DISPATCHER_H
#define DISPATCHER_CS_MSG_DISPATCHER_H

#pragma once

#include <config/compiler_features.h>
#include <design_pattern/singleton.h>

#include <google/protobuf/descriptor.h>

#include "dispatcher_implement.h"

namespace atbus {
    namespace protocol {
        class msg;
    }
} // namespace atbus

class cs_msg_dispatcher : public dispatcher_implement, public util::design_pattern::singleton<cs_msg_dispatcher> {
public:
    typedef dispatcher_implement::msg_op_type_t msg_op_type_t;
    typedef dispatcher_implement::msg_raw_t     msg_raw_t;
    typedef dispatcher_implement::msg_type_t    msg_type_t;

protected:
    cs_msg_dispatcher();

public:
    virtual ~cs_msg_dispatcher();
    virtual int32_t init() UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取任务信息
     * @param raw_msg 消息抽象结构
     * @return 相关的任务id
     */
    virtual uint64_t pick_msg_task_id(msg_raw_t &raw_msg) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取消息名称
     * @param raw_msg 消息抽象结构
     * @return 消息类型ID
     */
    virtual msg_type_t pick_msg_type_id(msg_raw_t &raw_msg) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取操作类型
     * @param raw_msg 消息抽象结构
     * @note 这只是一个调度曾规范，不强制执行。详情 @see hello::EnMsgOpType
     * @return 消息操作类型
     */
    virtual msg_op_type_t pick_msg_op_type(msg_raw_t &raw_msg) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 根据类型ID获取action或actor选项
     * @param raw_msg 消息抽象结构
     * @return 返回action或actor选项或NULL
     */
    virtual const hello::DDispatcherOptions* get_options_by_message_type(msg_type_t msg_type) UTIL_CONFIG_OVERRIDE;

    /**
     * deal with cs message data
     * @param msg msg information
     * @param buffer data
     * @param len data length
     * @return 0 or error code
     */
    int32_t dispatch(const atbus::protocol::msg &msg, const void *buffer, size_t len);

    /**
     * send kickoff message to atgateway
     * @param bus_id bus id of atgateway
     * @param session_id session id
     * @param reason kickoff reason
     * @return 0 or error code
     */
    int32_t send_kickoff(uint64_t bus_id, uint64_t session_id, int32_t reason);

    /**
     * send data to client
     * @param bus_id bus id of atgateway
     * @param session_id session id
     * @param buffer data buffer
     * @param len data length
     * @return 0 or error code
     */
    int32_t send_data(uint64_t bus_id, uint64_t session_id, const void *buffer, size_t len);

    /**
     * broadcast data to atgateway
     * @param bus_id bus id of atgateway
     * @param buffer data buffer
     * @param len data length
     * @return 0 or error code
     */
    int32_t broadcast_data(uint64_t bus_id, const void *buffer, size_t len);

    /**
     * broadcast data to multiple clients
     * @param bus_id bus id of atgateway
     * @param session_ids session id
     * @param buffer data buffer
     * @param len data length
     * @return 0 or error code
     */
    int32_t broadcast_data(uint64_t bus_id, const std::vector<uint64_t> &session_ids, const void *buffer, size_t len);
};


#endif // ATF4G_CO_CS_MSG_DISPATCHER_H
