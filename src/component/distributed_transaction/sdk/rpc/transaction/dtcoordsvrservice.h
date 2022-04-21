// Copyright 2022 atframework
// @brief Created by generate-for-pb.py for atframework.distributed_system.DtcoordsvrService, please don't edit it

#ifndef GENERATED_API_RPC_TRANSACTION_DTCOORDSVRSERVICE_H
#define GENERATED_API_RPC_TRANSACTION_DTCOORDSVRSERVICE_H

#pragma once


#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>
#include <pbdesc/distributed_transaction.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <string>

#include "rpc/rpc_common_types.h"

namespace rpc {
class context;
namespace transaction {

// ============ atframework.distributed_system.DtcoordsvrService.query ============
namespace packer {
bool pack_query(std::string& output, const atframework::distributed_system::SSDistributeTransactionQueryReq& input);
bool unpack_query(const std::string& input, atframework::distributed_system::SSDistributeTransactionQueryReq& output);
bool pack_query(std::string& output, const atframework::distributed_system::SSDistributeTransactionQueryRsp& input);
bool unpack_query(const std::string& input, atframework::distributed_system::SSDistributeTransactionQueryRsp& output);
}  // namespace packer

/**
 * @brief 拉取事务数据
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type query(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionQueryReq &req_body, atframework::distributed_system::SSDistributeTransactionQueryRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);

// ============ atframework.distributed_system.DtcoordsvrService.create ============
namespace packer {
bool pack_create(std::string& output, const atframework::distributed_system::SSDistributeTransactionCreateReq& input);
bool unpack_create(const std::string& input, atframework::distributed_system::SSDistributeTransactionCreateReq& output);
bool pack_create(std::string& output, const atframework::distributed_system::SSDistributeTransactionCreateRsp& input);
bool unpack_create(const std::string& input, atframework::distributed_system::SSDistributeTransactionCreateRsp& output);
}  // namespace packer

/**
 * @brief 创建分布式事务
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type create(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionCreateReq &req_body, atframework::distributed_system::SSDistributeTransactionCreateRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);

// ============ atframework.distributed_system.DtcoordsvrService.commit ============
namespace packer {
bool pack_commit(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitReq& input);
bool unpack_commit(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitReq& output);
bool pack_commit(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitRsp& input);
bool unpack_commit(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitRsp& output);
}  // namespace packer

/**
 * @brief 分布式事务-提交
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type commit(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionCommitReq &req_body, atframework::distributed_system::SSDistributeTransactionCommitRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);

// ============ atframework.distributed_system.DtcoordsvrService.reject ============
namespace packer {
bool pack_reject(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectReq& input);
bool unpack_reject(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectReq& output);
bool pack_reject(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectRsp& input);
bool unpack_reject(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectRsp& output);
}  // namespace packer

/**
 * @brief 分布式事务-拒绝
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionRejectReq &req_body, atframework::distributed_system::SSDistributeTransactionRejectRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);

// ============ atframework.distributed_system.DtcoordsvrService.commit_participator ============
namespace packer {
bool pack_commit_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq& input);
bool unpack_commit_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq& output);
bool pack_commit_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp& input);
bool unpack_commit_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp& output);
}  // namespace packer

/**
 * @brief 分布式事务-单个参与者提交
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type commit_participator(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionCommitParticipatorReq &req_body, atframework::distributed_system::SSDistributeTransactionCommitParticipatorRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);

// ============ atframework.distributed_system.DtcoordsvrService.reject_participator ============
namespace packer {
bool pack_reject_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq& input);
bool unpack_reject_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq& output);
bool pack_reject_participator(std::string& output, const atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp& input);
bool unpack_reject_participator(const std::string& input, atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp& output);
}  // namespace packer

/**
 * @brief 分布式事务-单个参与者拒绝
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_participator(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionRejectParticipatorReq &req_body, atframework::distributed_system::SSDistributeTransactionRejectParticipatorRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);

// ============ atframework.distributed_system.DtcoordsvrService.remove ============
namespace packer {
bool pack_remove(std::string& output, const atframework::distributed_system::SSDistributeTransactionRemoveReq& input);
bool unpack_remove(const std::string& input, atframework::distributed_system::SSDistributeTransactionRemoveReq& output);
bool pack_remove(std::string& output, const atframework::distributed_system::SSDistributeTransactionRemoveRsp& input);
bool unpack_remove(const std::string& input, atframework::distributed_system::SSDistributeTransactionRemoveRsp& output);
}  // namespace packer

/**
 * @brief 强制删除分布式事务
 * @param __ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack
 * @param dst_bus_id     target server bus id
 * @param req_body       request body
 * @param rsp_body       response body
 * @param __no_wait           set true if not need to wait response
 * @param __wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR rpc::result_code_type remove(context& __ctx, uint64_t dst_bus_id, atframework::distributed_system::SSDistributeTransactionRemoveReq &req_body, atframework::distributed_system::SSDistributeTransactionRemoveRsp &rsp_body, bool __no_wait = false, uint64_t* __wait_later = nullptr);
}  // namespace transaction
}

#endif
