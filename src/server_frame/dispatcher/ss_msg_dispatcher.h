//
// Created by owt50 on 2016/9/27.
//

#ifndef DISPATCHER_SS_MSG_DISPATCHER_H
#define DISPATCHER_SS_MSG_DISPATCHER_H

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

namespace hello {
    class SSMsg;
}

class ss_msg_dispatcher : public dispatcher_implement, public util::design_pattern::singleton<ss_msg_dispatcher> {
public:
    typedef dispatcher_implement::msg_raw_t  msg_raw_t;
    typedef dispatcher_implement::msg_type_t msg_type_t;

protected:
    ss_msg_dispatcher();

public:
    virtual ~ss_msg_dispatcher();

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
     * deal with cs message data
     * @param msg msg information
     * @param buffer data
     * @param len data length
     * @return 0 or error code
     */
    int32_t dispatch(const atbus::protocol::msg &msg, const void *buffer, size_t len);

    /**
     * notify send failed
     * @param msg msg information
     * @param buffer data
     * @param len data length
     * @return 0 or error code
     */
    int32_t on_receive_send_data_response(const atbus::protocol::msg &msg);

public:
    int32_t send_to_proc(uint64_t bus_id, const hello::SSMsg &ss_msg);
    int32_t send_to_proc(uint64_t bus_id, const void *msg_buf, size_t msg_len);


private:
    const google::protobuf::OneofDescriptor *get_body_oneof_desc() const;
};


#endif // ATF4G_CO_SS_MSG_DISPATCHER_H
