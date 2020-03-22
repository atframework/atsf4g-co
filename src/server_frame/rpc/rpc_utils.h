//
// Created by owent on 2016/10/4.
//

#ifndef RPC_RPC_UTILS_H
#define RPC_RPC_UTILS_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>


namespace hello {
    class SSMsg;
    class table_all_message;
} // namespace hello

namespace rpc {
    int wait(hello::SSMsg &msg);
    int wait(hello::table_all_message &msg);
} // namespace rpc

#endif //_RPC_RPC_UTILS_H
