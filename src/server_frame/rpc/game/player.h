//
// Created by owt50 on 2016/9/28.
//

#ifndef RPC_GAME_PLAYER_H
#define RPC_GAME_PLAYER_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>


namespace rpc {
    namespace game {
        namespace player {
            /**
             * @brief kickoff RPC
             * @param dst_bus_id server bus id
             * @param user_id player's user id
             * @param zone_id player's zone id
             * @param openid player's openid
             * @param reason kickoff reason
             * @return 0 or error code
             */
            int send_kickoff(uint64_t dst_bus_id, uint64_t user_id, uint32_t zone_id, const std::string &openid, int32_t reason = 0);

        } // namespace player
    }     // namespace game
} // namespace rpc


#endif //_RPC_GAME_PLAYER_H
