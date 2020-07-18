//
// Created by owt50 on 2016/10/9.
//

#ifndef RPC_DB_UUID_H
#define RPC_DB_UUID_H

#pragma once

#include <string>

#include <config/server_frame_build_feature.h>

namespace rpc {
    namespace db {
        namespace uuid {
#if defined(LIBATFRAME_UTILS_ENABLE_UUID) && LIBATFRAME_UTILS_ENABLE_UUID
            /**
             * 生成指定类型的UUID
             * @param uuid 输出生成的UUID
             * @return 0或错误码
             */
            int generate_standard_uuid(std::string &uuid);
#endif

            /**
             * 生成短UUID,和server id相关
             * @note 线程安全，但是一秒内的分配数量不能超过 2^32 个
             * @return 短UUID
             */
            std::string generate_short_uuid();

            /**
             * @biref 生成自增ID
             * @note 注意对于一组 (major_type, minor_type, patch_type) 如果用于这个接口，则不能用于下面的 generate_global_unique_id
             * @param major_type 主要类型
             * @param minor_type 次要类型(不需要可填0)
             * @param patch_type 补充类型(不需要可填0)
             * @return 如果成功，返回一个自增ID（正数），失败返回错误码，错误码 <= 0，
             */
            int64_t generate_global_increase_id(uint32_t major_type, uint32_t minor_type, uint32_t patch_type);

            /**
             * @biref 生成唯一ID
             * @note 注意对于一组 (major_type, minor_type, patch_type) 如果用于这个接口，则不能用于上面的 generate_global_increase_id
             * @note
             * 采用池化技术，当前配置中每组约8000个ID，每组分配仅访问一次数据库。并发情况下能够支撑40000个ID分配（时间单位取决于数据库延迟，一般100毫秒内）
             * @param major_type 主要类型
             * @param minor_type 次要类型(不需要可填0)
             * @param patch_type 补充类型(不需要可填0)
             * @return 如果成功，返回一个自增ID（正数），失败返回错误码，错误码 <= 0，
             */
            int64_t generate_global_unique_id(uint32_t major_type, uint32_t minor_type = 0, uint32_t patch_type = 0);
        } // namespace uuid

    } // namespace db
} // namespace rpc

#endif //_RPC_DB_UUID_H
