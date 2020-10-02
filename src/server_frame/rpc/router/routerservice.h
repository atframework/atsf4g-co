/**
 * @brief Created by generate-for-pb.py for hello.RouterService, please don't edit it
 */

#ifndef GENERATED_API_RPC_ROUTER_ROUTERSERVICE_H
#define GENERATED_API_RPC_ROUTER_ROUTERSERVICE_H

#pragma once


#include <cstddef>
#include <stdint.h>
#include <cstring>
#include <string>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

namespace rpc {
    class context;
    namespace router {

        // ============ hello.RouterService.router_update_sync ============
        /**
         * @brief 通知路由表更新
         * @param dst_bus_id     target server bus id
         * @param req_body       request body
         * @note  notify another server instance to update router table
         * @return 0 or error code
         */
        int router_update_sync(context& ctx, uint64_t dst_bus_id, hello::SSRouterUpdateSync &req_body);

        // ============ hello.RouterService.router_transfer ============
        /**
         * @brief 路由对象转移
         * @param dst_bus_id     target server bus id
         * @param req_body       request body
         * @param rsp_body       response body
         * @note  transfer a router object into another server instance
         * @return 0 or error code
         */
        int router_transfer(context& ctx, uint64_t dst_bus_id, hello::SSRouterTransferReq &req_body, hello::SSRouterTransferRsp &rsp_body);
    }
}

#endif
