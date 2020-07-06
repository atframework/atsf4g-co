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

            /**
             * @brief 分配User ID
             * @param out player's user id(最后3个bits是校验位)
             * @note 我们取2^5作为一个池的分配数量，这样当QPS为1K时能承载30/s的分配量。
             *       即便是大批玩家涌入比较极端的情况下，数据库访问30k/s时，能提供百万级的分配QPS。
             * @return allocated user id or error code(< 0)
             */
            int64_t alloc_user_id();

            /**
             * @brief 检测User ID是否合法
             * @param in player's account id
             * @return true or false
             */
            bool is_valid_user_id(int64_t in);
        } // namespace player
    }     // namespace game
} // namespace rpc


#endif //_RPC_GAME_PLAYER_H
