//
// Created by owt50 on 2018/04/06.
//

#ifndef DISPATCHER_DISPATCHER_TYPE_DEFINES_H
#define DISPATCHER_DISPATCHER_TYPE_DEFINES_H

#pragma once

#include <cstddef>
#include <stdint.h>

#include <config/compiler_features.h>
#include <std/explicit_declare.h>

struct dispatcher_msg_raw_t {
    uintptr_t msg_type; // 建议对所有的消息体类型分配一个ID，用以检查回调类型转换。推荐时使用dispatcher单例的地址。
    void *    msg_addr;
};

struct dispatcher_resume_data_t {
    dispatcher_msg_raw_t message;      // 异步回调中用于透传消息体
    void *               private_data; // 异步回调中用于透传额外的私有数据
};

struct dispatcher_start_data_t {
    dispatcher_msg_raw_t message;      // 启动回调中用于透传消息体
    void *               private_data; // 启动回调中用于透传额外的私有数据
};

#endif // DISPATCHER_DISPATCHER_TYPE_DEFINES_H
