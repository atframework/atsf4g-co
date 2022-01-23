// Copyright 2021 atframework
// Created by owentou on 2019/06/17.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

namespace rpc {
class context;

namespace db {
namespace async_jobs {

struct async_jobs_record {
  int64_t record_index;
  int64_t version;
  PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data action_blob;
};

/**
 * @brief 获取用户异步任务表所有数据的rpc操作
 * @param jobs_type 任务类型
 * @param user_id 用户的user_id
 * @param zone_id 大区ID
 * @param openid 用户的openid
 * @param out 返回的玩家数据
 * @return 0或错误码
 */
int get_jobs(::rpc::context &ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
             std::vector<async_jobs_record> &out);

/**
 * @brief 删除用户异步任务表指定任务数据的rpc操作
 * @param jobs_type 任务类型
 * @param user_id 用户的user_id
 * @param zone_id 大区ID
 * @param openid 用户的openid
 * @param in 要删除的下标
 * @return 0或错误码
 */
int del_jobs(::rpc::context &ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
             const std::vector<int64_t> &in);

/**
 * @brief 删除用户表所有数据的rpc操作
 * @param jobs_type 任务类型
 * @param user_id 用户的user_id
 * @param inout 待添加的数据
 * @note 最大异步任务数量配置在tcaplus的list表中。采用tcaplus的自动覆盖老记录的策略
 * @return 0或错误码
 */
int add_jobs(::rpc::context &ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
             PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data &in);

/**
 * @brief 删除用户异步任务表所有数据的rpc操作
 * @param jobs_type 任务类型
 * @param user_id 用户的user_id
 * @param zone_id 大区ID
 * @param openid 用户的openid
 * @return 0或错误码
 */
int remove_all_jobs(::rpc::context &ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id);

/**
 * @brief 更新用户异步任务表单条记录的rpc操作
 * @param jobs_type 任务类型
 * @param user_id 用户的user_id
 * @param openid 用户的openid
 * @param zone_id 大区ID
 * @param inout 待更新的数据
 * @param record_index 待更新的数据所在的数据库下标
 * @param version 版本号
 * @return 0或错误码
 */
int update_jobs(::rpc::context &ctx, int32_t jobs_type, uint64_t user_id, uint32_t zone_id,
                PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data &inout, int64_t record_index,
                int64_t *version = nullptr);
}  // namespace async_jobs
}  // namespace db
}  // namespace rpc
