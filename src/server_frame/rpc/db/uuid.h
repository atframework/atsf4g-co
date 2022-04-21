// Copyright 2022 atframework
// Created by owent on 2016-10-09.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <string>

#include "rpc/db/db_utils.h"

namespace rpc {
class context;

namespace db {
namespace uuid {
/**
 * @brief 生成标准UUID
 * @param remove_minus 是否移除减号(存hex模式输出)
 * @note 符合RFC4122标准，变种 1: 基于本地MAC地址和时间，时间周期为100纳秒，随机数部分为14位
 *       如果本地存在libuuid会复用libuuid的clock缓存文件: /var/lib/libuuid/clock.txt
 *       如果clock缓存文件不存在，100纳秒内分配N个uuid则有 (1-1/2^14)^(N-1) 的概率不冲突
 *       性能数据请参考 util::random::generate_string_time 的注解
 * @see util::random::generate_string_time
 * @see https://tools.ietf.org/html/rfc4122
 * @see https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_1_(date-time_and_MAC_address)
 * @return 生成的UUID
 */
EXPLICIT_NODISCARD_ATTR std::string generate_standard_uuid(bool remove_minus = false);

/**
 * @brief 生成标准UUID,返回二进制
 * @note 符合RFC4122标准，变种 1: 基于本地MAC地址和时间，时间周期为100纳秒，随机数部分为14位
 *       如果本地存在libuuid会复用libuuid的clock缓存文件: /var/lib/libuuid/clock.txt
 *       如果clock缓存文件不存在，100纳秒内分配N个uuid则有 (1-1/2^14)^(N-1) 的概率不冲突
 *       性能数据请参考 util::random::generate_string_time 的注解
 * @see util::random::generate_string_time
 * @see https://tools.ietf.org/html/rfc4122
 * @see https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_1_(date-time_and_MAC_address)
 * @return 生成的UUID,返回二进制
 */
EXPLICIT_NODISCARD_ATTR std::string generate_standard_uuid_binary();

/**
 * 生成短UUID,和server id相关
 * @note 线程安全，但是一秒内的分配数量不能超过 2^32 个
 * @return 短UUID
 */
EXPLICIT_NODISCARD_ATTR std::string generate_short_uuid();

/**
 * @biref 生成自增ID
 * @note 注意对于一组 (major_type, minor_type, patch_type) 如果用于这个接口，则不能用于下面的 generate_global_unique_id
 * @param major_type 主要类型
 * @param minor_type 次要类型(不需要可填0)
 * @param patch_type 补充类型(不需要可填0)
 * @return 如果成功，返回一个自增ID（正数），失败返回错误码，错误码 <= 0，
 */
EXPLICIT_NODISCARD_ATTR int64_t generate_global_increase_id(rpc::context &ctx, uint32_t major_type, uint32_t minor_type,
                                                            uint32_t patch_type);

/**
 * @biref 生成唯一ID
 * @note 注意对于一组 (major_type, minor_type, patch_type) 如果用于这个接口，则不能用于上面的
 * generate_global_increase_id
 * @note
 * 采用池化技术，当前配置中每组约8000个ID，每组分配仅访问一次数据库。并发情况下能够支撑40000个ID分配（时间单位取决于数据库延迟，一般100毫秒内）
 * @param major_type 主要类型
 * @param minor_type 次要类型(不需要可填0)
 * @param patch_type 补充类型(不需要可填0)
 * @return 如果成功，返回一个自增ID（正数），失败返回错误码，错误码 <= 0，
 */
EXPLICIT_NODISCARD_ATTR int64_t generate_global_unique_id(rpc::context &ctx, uint32_t major_type,
                                                          uint32_t minor_type = 0, uint32_t patch_type = 0);
}  // namespace uuid

}  // namespace db
}  // namespace rpc
