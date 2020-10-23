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

#include <libcopp/future/poll.h>

namespace rpc {
    class context;
    namespace router {
        struct routerservice_result_t {
            routerservice_result_t();
            routerservice_result_t(int code);
            operator int() const LIBCOPP_MACRO_NOEXCEPT;

            bool is_success() const LIBCOPP_MACRO_NOEXCEPT;
            bool is_error() const LIBCOPP_MACRO_NOEXCEPT;

            copp::future::poll_t<int> result;
        };

        // ============ hello.RouterService.router_update_sync ============
        /**
         * @brief 通知路由表更新
         * @param __ctx          RPC context, you can get it from get_shared_context() of task_action or just create one on stack
         * @param dst_bus_id     target server bus id
         * @param req_body       request body
         * @note  notify another server instance to update router table
         * @return 0 or error code
         */
        routerservice_result_t router_update_sync(context& __ctx, uint64_t dst_bus_id, hello::SSRouterUpdateSync &req_body);

        // ============ hello.RouterService.router_transfer ============
        /**
         * @brief 路由对象转移
         * @param __ctx          RPC context, you can get it from get_shared_context() of task_action or just create one on stack
         * @param dst_bus_id     target server bus id
         * @param req_body       request body
         * @param rsp_body       response body
         * @note  transfer a router object into another server instance
         * @return 0 or error code
         */
        routerservice_result_t router_transfer(context& __ctx, uint64_t dst_bus_id, hello::SSRouterTransferReq &req_body, hello::SSRouterTransferRsp &rsp_body);
    } // namespace router
}

#endif
