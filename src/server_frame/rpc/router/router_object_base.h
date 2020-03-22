//
// Created by owent on 2018/05/01.
//

#ifndef RPC_ROUTER_ROUTER_OBJECT_BASE_H
#define RPC_ROUTER_ROUTER_OBJECT_BASE_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <string>


#include <protocol/pbdesc/svr.protocol.pb.h>

namespace rpc {
    namespace router {
        namespace robj {
            /**
             * @brief 发送路由更新通知
             * @note 本接口不要求在协程中执行
             * @param dst_bus_id 发送目标的BUS ID
             * @param req 请求包
             * @return 0 or error code
             */
            int send_update_sync(uint64_t dst_bus_id, hello::SSRouterUpdateSync &req);

            /**
             * @brief 发送路由转移通知
             * @param dst_bus_id 发送目标的BUS ID
             * @param req 请求包
             * @param rsp 回包
             * @return 0 or error code
             */
            int send_transfer(uint64_t dst_bus_id, hello::SSRouterTransferReq &req, hello::SSRouterTransferRsp &rsp);
        } // namespace robj
    }     // namespace router
} // namespace rpc

#endif //_RPC_MAP_WATCH_H
