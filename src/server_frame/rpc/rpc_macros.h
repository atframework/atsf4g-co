//
// Created by owent on 2016/10/4.
//

#ifndef RPC_RPC_MACROS_H
#define RPC_RPC_MACROS_H

#pragma once

#include <stdint.h>
#include <cstddef>

namespace rpc {
    namespace ss {
        template<typename TReq, typename TRsp>
        int32_t call(uint64_t bus_id, TReq& req_msg, TRsp& rsp_msg);
    }
}
#endif //_RPC_RPC_MACROS_H
