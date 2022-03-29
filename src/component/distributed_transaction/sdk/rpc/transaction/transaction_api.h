// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <std/explicit_declare.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <unordered_set>

namespace rpc {
class context;
namespace transaction_api {

/**
 * @brief 初始化事务数据
 *
 * @param ctx RPC 上下文
 * @param inout 事务数据存储区
 * @param timeout 超时时间
 * @param replication_read_count 一致性模型: 读副本数
 * @param replication_total_count 一致性模型: 总副本数
 * @param memory_only 内存模式，不提交到数据库
 * @param force_commit 强制提交，不需要协调者
 *
 * @note 当前一致性模型采用依赖数据库CAS操作或Read-your-writes模型。
 *       当replication_read_count=0且replication_total_count=0时，事务系统依赖数据库CAS操作来实现一致性保证。
 *       当replication_read_count>0且replication_total_count>=当replication_read_count时，事务系统采用Read-your-writes模型。
 *       此时replication_read_count为Read-your-writes的读参数(R),replication_total_count为Read-your-writes的总副本数(N)。
 *       如果事务协调者服务进程数少于replication_total_count，则replication_total_count会自动调整为协调者服务进程数。
 *
 * @see http://www.dbms2.com/2010/05/01/ryw-read-your-writes-consistency/
 * @see https://en.wikipedia.org/wiki/Consistency_model#Client-centric_consistency_models
 * @return future of 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type initialize_new_transaction(
    rpc::context& ctx, atframework::distributed_system::transaction_blob_storage& inout,
    const google::protobuf::Duration& timeout, uint32_t replication_read_count = 0,
    uint32_t replication_total_count = 0, bool memory_only = false, bool force_commit = false);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type query_transaction(
    rpc::context& ctx, const atframework::distributed_system::transaction_metadata& metadata,
    atframework::distributed_system::transaction_blob_storage& out);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type create_transaction(
    rpc::context& ctx, atframework::distributed_system::transaction_blob_storage& inout);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type commit_transaction(
    rpc::context& ctx, atframework::distributed_system::transaction_metadata& inout);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_transaction(
    rpc::context& ctx, atframework::distributed_system::transaction_metadata& inout);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_transaction_no_wait(
    rpc::context& ctx, const atframework::distributed_system::transaction_metadata& metadata);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove_transaction(
    rpc::context& ctx, const atframework::distributed_system::transaction_metadata& metadata);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type commit_participator(
    rpc::context& ctx, const std::string& participator_key,
    atframework::distributed_system::transaction_metadata& inout);

EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_participator(
    rpc::context& ctx, const std::string& participator_key,
    atframework::distributed_system::transaction_metadata& inout);

void merge_storage(atframework::distributed_system::transaction_blob_storage& output,
                   const atframework::distributed_system::transaction_blob_storage& input);

void merge_storage(const std::string& participator_key,
                   atframework::distributed_system::transaction_participator_storage& output,
                   const atframework::distributed_system::transaction_blob_storage& input);

void pack_participator_request(atframework::distributed_system::SSParticipatorTransactionPrepareReq& output,
                               const atframework::distributed_system::transaction_blob_storage& input_transaction,
                               const atframework::distributed_system::transaction_participator& input_participator);

void pack_participator_request(atframework::distributed_system::SSParticipatorTransactionCommitReq& output,
                               const atframework::distributed_system::transaction_blob_storage& input_transaction,
                               const atframework::distributed_system::transaction_participator& input_participator);

void pack_participator_request(atframework::distributed_system::SSParticipatorTransactionRejectReq& output,
                               const atframework::distributed_system::transaction_blob_storage& input_transaction,
                               const atframework::distributed_system::transaction_participator& input_participator);

}  // namespace transaction_api
}  // namespace rpc
