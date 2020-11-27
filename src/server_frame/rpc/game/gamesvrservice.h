/**
 * @brief Created by generate-for-pb.py for hello.GamesvrService, please don't edit it
 */

#ifndef GENERATED_API_RPC_GAME_GAMESVRSERVICE_H
#define GENERATED_API_RPC_GAME_GAMESVRSERVICE_H

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
    namespace game {
        struct gamesvrservice_result_t {
            gamesvrservice_result_t();
            gamesvrservice_result_t(int code);
            operator int() const LIBCOPP_MACRO_NOEXCEPT;

            bool is_success() const LIBCOPP_MACRO_NOEXCEPT;
            bool is_error() const LIBCOPP_MACRO_NOEXCEPT;

            copp::future::poll_t<int> result;
        };

        // ============ hello.GamesvrService.player_kickoff ============
        /**
         * @brief 通知提用户下线
         * @param __ctx          RPC context, you can get it from get_shared_context() of task_action or just create one on stack
         * @param dst_bus_id     target server bus id
         * @param zone_id        zone id that will be passsed into header
         * @param user_id        user id that will be passsed into header
         * @param open_id        open id that will be passsed into header
         * @param req_body       request body
         * @param rsp_body       response body
         * @return 0 or error code
         */
        gamesvrservice_result_t player_kickoff(context& __ctx, uint64_t dst_bus_id, uint32_t zone_id, uint64_t user_id, const std::string& open_id, hello::SSPlayerKickOffReq &req_body, hello::SSPlayerKickOffRsp &rsp_body);
    } // namespace game
}

#endif
