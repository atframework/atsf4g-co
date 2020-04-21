//
// Created by owt50 on 2018/04/06.
//

#ifndef DISPATCHER_DISPATCHER_TYPE_DEFINES_H
#define DISPATCHER_DISPATCHER_TYPE_DEFINES_H

#pragma once

#include <cstddef>
#include <cstdlib>
#include <stdint.h>

#include <config/compiler_features.h>
#include <std/explicit_declare.h>

namespace hello {
    class DDispatcherOptions;
}

struct dispatcher_msg_raw_t {
    uintptr_t msg_type; // 建议对所有的消息体类型分配一个ID，用以检查回调类型转换。推荐时使用dispatcher单例的地址。
    void *    msg_addr;
};

struct dispatcher_resume_data_t {
    dispatcher_msg_raw_t message;      // 异步回调中用于透传消息体
    void *               private_data; // 异步回调中用于透传额外的私有数据
    uint64_t             sequence;     // 等待序号，通常和发送序号匹配。用于检测同类消息是否是发出的那个
};

struct dispatcher_start_data_t {
    dispatcher_msg_raw_t message;      // 启动回调中用于透传消息体
    void *               private_data; // 启动回调中用于透传额外的私有数据
    const hello::DDispatcherOptions* dispatcher_options; // 调度协议层选项
};


template<class T>
T dispatcher_make_default();

template<>
inline dispatcher_msg_raw_t dispatcher_make_default<dispatcher_msg_raw_t>() {
    dispatcher_msg_raw_t ret;
    ret.msg_addr = NULL;
    ret.msg_type = 0;
    return ret;
}

template<>
inline dispatcher_start_data_t dispatcher_make_default<dispatcher_start_data_t>() {
    dispatcher_start_data_t ret;
    ret.message = dispatcher_make_default<dispatcher_msg_raw_t>();
    ret.private_data = NULL;
    ret.dispatcher_options = NULL;
    return ret;
}

template<>
inline dispatcher_resume_data_t dispatcher_make_default<dispatcher_resume_data_t>() {
    dispatcher_resume_data_t ret;
    ret.message = dispatcher_make_default<dispatcher_msg_raw_t>();
    ret.private_data = NULL;
    ret.sequence = 0;
    return ret;
}

#endif // DISPATCHER_DISPATCHER_TYPE_DEFINES_H
