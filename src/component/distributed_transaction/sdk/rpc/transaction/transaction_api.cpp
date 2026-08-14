// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#include "rpc/transaction/transaction_api.h"

#include <std/explicit_declare.h>

#include <log/log_wrapper.h>

#include <algorithm/base64.h>
#include <algorithm/murmur_hash.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <atframe/etcdcli/etcd_discovery.h>

#include <utility/random_engine.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/extern_service_types.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>

#include <logic/logic_server_setup.h>

#include <rpc/db/uuid.h>
#include <rpc/rpc_utils.h>

#include <utility/protobuf_mini_dumper.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "rpc/transaction/dtcoordsvrservice.atfw.gen.h"

#define TRANSACTION_API_RETRY_TIMES 5

namespace rpc {
namespace transaction_api {
namespace {
static uint64_t calculate_server_id(const atfw::distributed_system::transaction_metadata& metadata) {
  logic_server_common_module* common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    return 0;
  }

  for (int i = 0; i < metadata.replicate_node_server_id_size(); ++i) {
    if (nullptr != common_mod->get_discovery_by_id(metadata.replicate_node_server_id(i))) {
      return metadata.replicate_node_server_id(i);
    }
  }

  if (metadata.transaction_uuid().empty()) {
    return 0;
  }

  auto discovery = common_mod->get_discovery_index_by_type_zone(
      static_cast<uint64_t>(atframework::component::logic_service_type::kDtCoordSvr),
      logic_config::me()->get_local_zone_id());
  if (!discovery || discovery->empty()) {
    discovery = common_mod->get_discovery_index_by_type(
        static_cast<uint64_t>(atframework::component::logic_service_type::kDtCoordSvr));
  }

  if (!discovery || discovery->empty()) {
    return 0;
  }

  atfw::atapp::etcd_discovery_node::ptr_t node = discovery->get_node_by_consistent_hash(metadata.transaction_uuid());
  if (!node) {
    return 0;
  }
  return node->get_discovery_info().id();
}

static void initialize_replication_server_ids(atfw::distributed_system::transaction_metadata& metadata,
                                              uint32_t& replication_read_count, uint32_t& replication_total_count) {
  if (replication_read_count <= 0 || replication_total_count < replication_read_count) {
    return;
  }
  logic_server_common_module* common_mod = logic_server_last_common_module();
  if (nullptr == common_mod) {
    return;
  }

  auto discovery = common_mod->get_discovery_index_by_type_zone(
      static_cast<uint64_t>(atframework::component::logic_service_type::kDtCoordSvr),
      logic_config::me()->get_local_zone_id());
  if (!discovery || discovery->empty()) {
    discovery = common_mod->get_discovery_index_by_type(
        static_cast<uint64_t>(atframework::component::logic_service_type::kDtCoordSvr));
  }

  if (!discovery || discovery->empty()) {
    return;
  }

  const auto& sorted_nodes = discovery->get_sorted_nodes();
  if (sorted_nodes.empty()) {
    return;
  }

  if (sorted_nodes.size() < replication_total_count) {
    replication_total_count = static_cast<uint32_t>(sorted_nodes.size());
  }
  if (replication_read_count > replication_total_count) {
    replication_read_count = replication_total_count;
  }
  uint64_t hash_out[2];
  atfw::util::hash::murmur_hash3_x64_128(metadata.transaction_uuid().c_str(),
                                         static_cast<int>(metadata.transaction_uuid().size()),
                                         LIBATAPP_MACRO_HASH_MAGIC_NUMBER, hash_out);
  size_t current_index = hash_out[0] % sorted_nodes.size();
  metadata.mutable_replicate_node_server_id()->Reserve(static_cast<int32_t>(replication_total_count));
  metadata.set_replicate_read_count(replication_read_count);
  for (uint32_t i = 0; i < replication_total_count || (replication_total_count <= 0 && i < 3); ++i, ++current_index) {
    if (current_index >= sorted_nodes.size()) {
      current_index = 0;
    }
    metadata.add_replicate_node_server_id(sorted_nodes[current_index]->get_discovery_info().id());
  }
}

static bool is_replication_mode(const atfw::distributed_system::transaction_metadata& metadata) {
  return metadata.replicate_read_count() > 0 &&
         static_cast<uint32_t>(metadata.replicate_node_server_id_size()) >= metadata.replicate_read_count();
}

static void merge_transaction_metadata(atfw::distributed_system::transaction_metadata& output,
                                       const atfw::distributed_system::transaction_metadata& input) {
  // Merge metadata
  if (output.transaction_uuid().empty() && !input.transaction_uuid().empty()) {
    output.set_transaction_uuid(input.transaction_uuid());
  }

  if (output.status() < input.status()) {
    output.set_status(input.status());
  }

  if (output.replicate_read_count() == 0) {
    output.set_replicate_read_count(input.replicate_read_count());
  }

  if (output.replicate_node_server_id_size() == 0 && input.replicate_node_server_id_size() != 0) {
    protobuf_copy_message(*output.mutable_replicate_node_server_id(), input.replicate_node_server_id());
  }

  if (output.prepare_timepoint().seconds() == 0 && output.prepare_timepoint().nanos() == 0) {
    protobuf_copy_message(*output.mutable_prepare_timepoint(), input.prepare_timepoint());
  }

  if (output.finish_timepoint().seconds() == 0 && output.finish_timepoint().nanos() == 0) {
    protobuf_copy_message(*output.mutable_finish_timepoint(), input.finish_timepoint());
  }

  if (output.expire_timepoint().seconds() == 0 && output.expire_timepoint().nanos() == 0) {
    protobuf_copy_message(*output.mutable_expire_timepoint(), input.expire_timepoint());
  }
}

static void merge_transaction_configure(atfw::distributed_system::transaction_configure& output,
                                        const atfw::distributed_system::transaction_configure& input) {
  if (output.resolve_max_times() == 0) {
    output.set_resolve_max_times(input.resolve_max_times());
  }

  if (output.lock_retry_max_times() == 0) {
    output.set_lock_retry_max_times(input.lock_retry_max_times());
  }

  if (output.resolve_retry_interval().seconds() == 0 && output.resolve_retry_interval().nanos() == 0) {
    protobuf_copy_message(*output.mutable_resolve_retry_interval(), input.resolve_retry_interval());
  }

  if (output.lock_wait_interval_min().seconds() == 0 && output.lock_wait_interval_min().nanos() == 0) {
    protobuf_copy_message(*output.mutable_lock_wait_interval_min(), input.lock_wait_interval_min());
  }
  if (output.lock_wait_interval_max().seconds() == 0 && output.lock_wait_interval_max().nanos() == 0) {
    protobuf_copy_message(*output.mutable_lock_wait_interval_max(), input.lock_wait_interval_max());
  }
}

static inline void merge_transaction_participator(::atfw::distributed_system::transaction_participator& output,
                                                  const ::atfw::distributed_system::transaction_participator& input) {
  if (output.participator_status() < input.participator_status()) {
    output.set_participator_status(input.participator_status());
  }
  if ((!output.has_participator_data() || output.participator_data().type_url().empty()) &&
      !input.participator_data().type_url().empty()) {
    protobuf_copy_message(*output.mutable_participator_data(), input.participator_data());
  }
}

static void merge_transaction_participators(
    google::protobuf::Map<std::string, ::atfw::distributed_system::transaction_participator>& output,
    const google::protobuf::Map<std::string, ::atfw::distributed_system::transaction_participator>& input) {
  for (const auto& participator : input) {
    auto output_iter = output.find(participator.first);
    if (output_iter == output.end()) {
      protobuf_copy_message(output[participator.first], participator.second);
    } else {
      merge_transaction_participator(output_iter->second, participator.second);
    }
  }
}

static void merge_transaction_storage(atfw::distributed_system::transaction_blob_storage& output,
                                      const atfw::distributed_system::transaction_blob_storage& input) {
  // Merge metadata
  if (!output.has_metadata() && input.has_metadata()) {
    protobuf_copy_message(*output.mutable_metadata(), input.metadata());
  } else {
    merge_transaction_metadata(*output.mutable_metadata(), input.metadata());
  }

  // Merge configure
  if (!output.has_configure() && input.has_configure()) {
    protobuf_copy_message(*output.mutable_configure(), input.configure());
  } else {
    merge_transaction_configure(*output.mutable_configure(), input.configure());
  }

  // Merge transaction data
  if ((!output.has_transaction_data() || output.transaction_data().type_url().empty()) &&
      !input.transaction_data().type_url().empty()) {
    protobuf_copy_message(*output.mutable_transaction_data(), input.transaction_data());
  }

  // Merge participators
  merge_transaction_participators(*output.mutable_participators(), input.participators());
}

static void merge_transaction_storage(const std::string& participator_key,
                                      atfw::distributed_system::transaction_participator_storage& output,
                                      const atfw::distributed_system::transaction_blob_storage& input) {
  auto iter = input.participators().find(participator_key);
  if (input.participators().end() == iter) {
    return;
  }

  // Merge metadata
  if (!output.has_metadata() && input.has_metadata()) {
    protobuf_copy_message(*output.mutable_metadata(), input.metadata());
  } else {
    merge_transaction_metadata(*output.mutable_metadata(), input.metadata());
  }

  // Merge configure
  if (!output.has_configure() && input.has_configure()) {
    protobuf_copy_message(*output.mutable_configure(), input.configure());
  } else {
    merge_transaction_configure(*output.mutable_configure(), input.configure());
  }

  // Merge transaction data
  if ((!output.has_transaction_data() || output.transaction_data().type_url().empty()) &&
      !input.transaction_data().type_url().empty()) {
    protobuf_copy_message(*output.mutable_transaction_data(), input.transaction_data());
  }

  // Merge participator data
  if ((!output.has_participator_data() || output.participator_data().type_url().empty()) &&
      !iter->second.participator_data().type_url().empty()) {
    protobuf_copy_message(*output.mutable_participator_data(), iter->second.participator_data());
  }
}

template <class TRequest, class TResponse, class TRpcFn>
static rpc::result_code_type invoke_replication_rpc_call(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata,
    rpc::context::message_holder<TRequest>& req_body, rpc::context::message_holder<TResponse>& rsp_body,
    gsl::string_view action_name, TRpcFn&& rpc_fn,
    std::function<bool(uint64_t, const atframework::SSMsg&)> on_receive_message_fn, bool no_wait = false) {
  std::unordered_set<dispatcher_await_options> waiters;
  std::unordered_map<uint64_t, atframework::SSMsg> received;
  const size_t required_success_count = metadata.replicate_read_count();
  size_t send_success_count = 0;
  size_t response_success_count = 0;
  int32_t last_error_res = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_RETRY_TIMES_EXCEED;
  waiters.reserve(static_cast<size_t>(metadata.replicate_node_server_id_size()));

  for (uint64_t target_server_id : metadata.replicate_node_server_id()) {
    dispatcher_await_options waiter_options = dispatcher_make_default<dispatcher_await_options>();
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc_fn(ctx, target_server_id, *req_body, *rsp_body, no_wait, &waiter_options));
    if (res < 0) {
      FWLOGERROR("Try to {} transaction {} from server {:#x} failed, res: {}({})", action_name,
                 metadata.transaction_uuid(), target_server_id, res, protobuf_mini_dumper_get_error_msg(res));
      last_error_res = res;
    } else {
      ++send_success_count;
      waiters.emplace(std::move(waiter_options));
    }
  }
  // No wait and send success should be treated as success
  if (no_wait) {
    if (send_success_count >= required_success_count) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    } else {
      RPC_RETURN_CODE(last_error_res);
    }
  }

  dispatcher_await_options one_waiter_options = dispatcher_make_default<dispatcher_await_options>();
  while (response_success_count < required_success_count) {
    const size_t required_response_count = required_success_count - response_success_count;
    if (waiters.size() < required_response_count) {
      RPC_RETURN_CODE(last_error_res);
    }
    received.clear();

    int32_t wait_result = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, waiters, received, required_response_count));
    if (wait_result < 0) {
      FWLOGERROR("Try to {} transaction {} and wait multiple response failed, res: {}({})", action_name,
                 metadata.transaction_uuid(), wait_result, protobuf_mini_dumper_get_error_msg(wait_result));
      RPC_RETURN_CODE(wait_result);
    }
    for (auto& received_message : received) {
      one_waiter_options.sequence = received_message.first;
      waiters.erase(one_waiter_options);

      if (received_message.second.head().error_code() != 0) {
        last_error_res = received_message.second.head().error_code();
        FWLOGERROR("Try to {} transaction {} and wait response of sequence {} failed, res: {}({})", action_name,
                   metadata.transaction_uuid(), received_message.first, received_message.second.head().error_code(),
                   protobuf_mini_dumper_get_error_msg(received_message.second.head().error_code()));
        continue;
      }

      if (on_receive_message_fn && !on_receive_message_fn(received_message.first, received_message.second)) {
        last_error_res = PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
        continue;
      }

      ++response_success_count;
    }
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type initialize_new_transaction(
    rpc::context&, atfw::distributed_system::transaction_blob_storage& inout,
    std::chrono::system_clock::duration timeout, uint32_t replication_read_count, uint32_t replication_total_count,
    bool memory_only, bool force_commit) {
  std::string trans_uuid;
  atfw::util::base64_encode(trans_uuid, rpc::db::uuid::generate_standard_uuid_binary(),
                            atfw::util::base64_mode_t::EN_BMT_UTF7);
  if (trans_uuid.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  atfw::distributed_system::transaction_metadata& metadata = *inout.mutable_metadata();
  atfw::distributed_system::transaction_configure& configure = *inout.mutable_configure();
  metadata.set_transaction_uuid(trans_uuid);
  auto now = util::time::time_utility::now();
  protobuf_copy_message(*metadata.mutable_prepare_timepoint(), protobuf_from_system_clock(now));
  if (timeout <= std::chrono::system_clock::duration::zero()) {
    timeout = std::chrono::seconds{10};
  }
  protobuf_copy_message(*metadata.mutable_expire_timepoint(), protobuf_from_system_clock(now + timeout));

  metadata.set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_CREATED);
  initialize_replication_server_ids(metadata, replication_read_count, replication_total_count);
  metadata.set_memory_only(memory_only);

  configure.set_force_commit(force_commit);
  if (configure.resolve_max_times() == 0) {
    configure.set_resolve_max_times(3);
  }

  if (configure.lock_retry_max_times() == 0) {
    configure.set_lock_retry_max_times(3);
  }

  if (protobuf_to_chrono_duration(configure.resolve_retry_interval()) <= std::chrono::system_clock::duration::zero()) {
    protobuf_copy_message(*configure.mutable_resolve_retry_interval(),
                          protobuf_from_chrono_duration(std::chrono::seconds{10}));
  }

  if (protobuf_to_chrono_duration(configure.lock_wait_interval_min()) <= std::chrono::system_clock::duration::zero()) {
    protobuf_copy_message(*configure.mutable_lock_wait_interval_min(),
                          protobuf_from_chrono_duration(std::chrono::milliseconds{32}));
  }

  if (protobuf_to_chrono_duration(configure.lock_wait_interval_max()) <= std::chrono::system_clock::duration::zero()) {
    protobuf_copy_message(*configure.mutable_lock_wait_interval_max(),
                          protobuf_from_chrono_duration(std::chrono::milliseconds{256}));
  }
  if (protobuf_to_chrono_duration(configure.lock_wait_interval_max()) <
      protobuf_to_chrono_duration(configure.lock_wait_interval_min())) {
    protobuf_copy_message(*configure.mutable_lock_wait_interval_max(), configure.lock_wait_interval_min());
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type query_transaction(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata,
    atfw::distributed_system::transaction_blob_storage& out) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={})",
                                       metadata.transaction_uuid());

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionQueryReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionQueryRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), metadata);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(metadata)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, metadata, req_body, rsp_body, "query", rpc::transaction::query,
        [&out, &rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          if (!rpc::transaction::packer::unpack_query(received_message.body_bin(), *rsp_body) ||
              !rsp_body->has_storage()) {
            return false;
          }
          merge_transaction_storage(out, rsp_body->storage());
          return true;
        })));
  } else {
    uint64_t target_server_id = calculate_server_id(metadata);
    if (0 == target_server_id) {
      FWLOGERROR("{} can not find any available server for transaction {}", __FUNCTION__, metadata.transaction_uuid());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::transaction::query(ctx, target_server_id, *req_body, *rsp_body));
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }

    protobuf_move_message(out, std::move(*rsp_body->mutable_storage()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type create_transaction(
    rpc::context& ctx, atfw::distributed_system::transaction_blob_storage& inout) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={})",
                                       inout.metadata().transaction_uuid());

  if (inout.configure().resolve_max_times() == 0 ||
      inout.metadata().status() != atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionCreateReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionCreateRsp> rsp_body(ctx);

  if (req_body->GetArena() == inout.GetArena()) {
    if (nullptr == req_body->GetArena()) {
      req_body->set_allocated_storage(&inout);
    } else {
      req_body->unsafe_arena_set_allocated_storage(&inout);
    }
  } else {
    protobuf_copy_message(*req_body->mutable_storage(), inout);
  }
  int res = 0;

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(inout.metadata())) {
    res = RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, inout.metadata(), req_body, rsp_body, "create", rpc::transaction::create,
        [&rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          return rpc::transaction::packer::unpack_create(received_message.body_bin(), *rsp_body);
        }));
  } else {
    uint64_t target_server_id = calculate_server_id(inout.metadata());
    if (0 == target_server_id) {
      FWLOGERROR("{} can not find any available server for transaction {}", __FUNCTION__,
                 inout.metadata().transaction_uuid());
      res = PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND;
    } else {
      res = RPC_AWAIT_CODE_RESULT(rpc::transaction::create(ctx, target_server_id, *req_body, *rsp_body));
    }
  }

  if (req_body->GetArena() == inout.GetArena()) {
    if (nullptr == req_body->GetArena()) {
      ATFW_EXPLICIT_UNUSED_ATTR auto* _storage = req_body->release_storage();
    } else {
      ATFW_EXPLICIT_UNUSED_ATTR auto* _storage = req_body->unsafe_arena_release_storage();
    }
  }

  RPC_RETURN_CODE(res);
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type commit_transaction(
    rpc::context& ctx, atfw::distributed_system::transaction_metadata& inout) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={})",
                                       inout.transaction_uuid());

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionCommitReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionCommitRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), inout);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(inout)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, inout, req_body, rsp_body, "commit", rpc::transaction::commit,
        [&inout, &rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          if (!rpc::transaction::packer::unpack_commit(received_message.body_bin(), *rsp_body) ||
              !rsp_body->has_metadata()) {
            return false;
          }
          merge_transaction_metadata(inout, rsp_body->metadata());
          return true;
        })));
  } else {
    int res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    int left_retry_times = TRANSACTION_API_RETRY_TIMES;
    // 如果事务服务器返回OLD_VERSION可能是发生了故障转移或者扩缩容，可以直接重试
    while (left_retry_times-- > 0) {
      uint64_t target_server_id = calculate_server_id(inout);
      if (0 == target_server_id) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::transaction::commit(ctx, target_server_id, *req_body, *rsp_body));
      if (res < 0) {
        if (res == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
          continue;
        }
        RPC_RETURN_CODE(res);
      }

      protobuf_move_message(inout, std::move(*rsp_body->mutable_metadata()));
      break;
    }

    RPC_RETURN_CODE(res);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type reject_transaction(
    rpc::context& ctx, atfw::distributed_system::transaction_metadata& inout) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={})",
                                       inout.transaction_uuid());

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRejectReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRejectRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), inout);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(inout)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, inout, req_body, rsp_body, "reject", rpc::transaction::reject,
        [&inout, &rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          if (!rpc::transaction::packer::unpack_reject(received_message.body_bin(), *rsp_body) ||
              !rsp_body->has_metadata()) {
            return false;
          }
          merge_transaction_metadata(inout, rsp_body->metadata());
          return true;
        })));
  } else {
    int res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    int left_retry_times = TRANSACTION_API_RETRY_TIMES;
    // 如果事务服务器返回OLD_VERSION可能是发生了故障转移或者扩缩容，可以直接重试
    while (left_retry_times-- > 0) {
      uint64_t target_server_id = calculate_server_id(inout);
      if (0 == target_server_id) {
        FWLOGERROR("{} can not find any available server for transaction {}", __FUNCTION__, inout.transaction_uuid());
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::transaction::reject(ctx, target_server_id, *req_body, *rsp_body));
      if (res < 0) {
        if (res == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
          continue;
        }
        RPC_RETURN_CODE(res);
      }

      protobuf_move_message(inout, std::move(*rsp_body->mutable_metadata()));
      break;
    }

    RPC_RETURN_CODE(res);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type remove_transaction_no_wait(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata) {
  if (metadata.transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRemoveReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRemoveRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), metadata);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(metadata)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(ctx, metadata, req_body, rsp_body, "remove",
                                                                      rpc::transaction::remove, nullptr, true)));
  } else {
    uint64_t target_server_id = calculate_server_id(metadata);
    if (0 == target_server_id) {
      FWLOGERROR("{} can not find any available server for transaction {}", __FUNCTION__, metadata.transaction_uuid());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
    }

    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::transaction::remove(ctx, target_server_id, *req_body, *rsp_body, true)));
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type remove_transaction(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={})",
                                       metadata.transaction_uuid());

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRemoveReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRemoveRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), metadata);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(metadata)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, metadata, req_body, rsp_body, "remove", rpc::transaction::remove,
        [&rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          return rpc::transaction::packer::unpack_remove(received_message.body_bin(), *rsp_body);
        })));
  } else {
    int res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    int left_retry_times = TRANSACTION_API_RETRY_TIMES;
    // 如果事务服务器返回OLD_VERSION可能是发生了故障转移或者扩缩容，可以直接重试
    while (left_retry_times-- > 0) {
      uint64_t target_server_id = calculate_server_id(metadata);
      if (0 == target_server_id) {
        FWLOGERROR("{} can not find any available server for transaction {}", __FUNCTION__,
                   metadata.transaction_uuid());
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::transaction::remove(ctx, target_server_id, *req_body, *rsp_body));
      if (res < 0) {
        if (res == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
          continue;
        }
        if (res == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        }
        RPC_RETURN_CODE(res);
      }

      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    RPC_RETURN_CODE(res);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type commit_participator(
    rpc::context& ctx, const std::string& participator_key, atfw::distributed_system::transaction_metadata& inout) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={}, participator={})",
                                       inout.transaction_uuid(), participator_key);

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionCommitParticipatorReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionCommitParticipatorRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), inout);
  req_body->set_participator_key(participator_key);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(inout)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, inout, req_body, rsp_body, "commit_participator", rpc::transaction::commit_participator,
        [&inout, &rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          if (!rpc::transaction::packer::unpack_commit_participator(received_message.body_bin(), *rsp_body) ||
              !rsp_body->has_metadata()) {
            return false;
          }
          merge_transaction_metadata(inout, rsp_body->metadata());
          return true;
        })));
  } else {
    int res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    int left_retry_times = TRANSACTION_API_RETRY_TIMES;
    // 如果事务服务器返回OLD_VERSION可能是发生了故障转移或者扩缩容，可以直接重试
    while (left_retry_times-- > 0) {
      uint64_t target_server_id = calculate_server_id(inout);
      if (0 == target_server_id) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::transaction::commit_participator(ctx, target_server_id, *req_body, *rsp_body));

      if (res < 0) {
        if (res == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
          continue;
        }
        RPC_RETURN_CODE(res);
      }

      protobuf_move_message(inout, std::move(*rsp_body->mutable_metadata()));
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    RPC_RETURN_CODE(res);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API rpc::result_code_type reject_participator(
    rpc::context& ctx, const std::string& participator_key, atfw::distributed_system::transaction_metadata& inout) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("this function must be called in a task(transaction_uuid={}, participator={})",
                                       inout.transaction_uuid(), participator_key);

  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRejectParticipatorReq> req_body(ctx);
  rpc::context::message_holder<atfw::distributed_system::SSDistributeTransactionRejectParticipatorRsp> rsp_body(ctx);

  protobuf_copy_message(*req_body->mutable_metadata(), inout);
  req_body->set_participator_key(participator_key);

  // Read-Your-Writes 一致性实现
  if (is_replication_mode(inout)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(invoke_replication_rpc_call(
        ctx, inout, req_body, rsp_body, "reject_participator", rpc::transaction::reject_participator,
        [&inout, &rsp_body](uint64_t, const atframework::SSMsg& received_message) -> bool {
          if (!rpc::transaction::packer::unpack_reject_participator(received_message.body_bin(), *rsp_body) ||
              !rsp_body->has_metadata()) {
            return false;
          }
          merge_transaction_metadata(inout, rsp_body->metadata());
          return true;
        })));
  } else {
    int res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    int left_retry_times = TRANSACTION_API_RETRY_TIMES;
    // 如果事务服务器返回OLD_VERSION可能是发生了故障转移或者扩缩容，可以直接重试
    while (left_retry_times-- > 0) {
      uint64_t target_server_id = calculate_server_id(inout);
      if (0 == target_server_id) {
        FWLOGERROR("{} can not find any available server for transaction {}", __FUNCTION__, inout.transaction_uuid());
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      res = RPC_AWAIT_CODE_RESULT(rpc::transaction::reject_participator(ctx, target_server_id, *req_body, *rsp_body));
      if (res < 0) {
        if (res == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
          continue;
        }
        RPC_RETURN_CODE(res);
      }

      protobuf_move_message(inout, std::move(*rsp_body->mutable_metadata()));
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    RPC_RETURN_CODE(res);
  }
}

DISTRIBUTED_TRANSACTION_SDK_API void merge_storage(atfw::distributed_system::transaction_blob_storage& output,
                                                   const atfw::distributed_system::transaction_blob_storage& input) {
  merge_transaction_storage(output, input);
}

DISTRIBUTED_TRANSACTION_SDK_API void merge_storage(const std::string& participator_key,
                                                   atfw::distributed_system::transaction_participator_storage& output,
                                                   const atfw::distributed_system::transaction_blob_storage& input) {
  merge_transaction_storage(participator_key, output, input);
}

DISTRIBUTED_TRANSACTION_SDK_API void pack_participator_request(
    atfw::distributed_system::SSParticipatorTransactionPrepareReq& output,
    const atfw::distributed_system::transaction_blob_storage& input_transaction,
    const atfw::distributed_system::transaction_participator& input_participator) {
  protobuf_copy_message(*output.mutable_storage()->mutable_metadata(), input_transaction.metadata());
  protobuf_copy_message(*output.mutable_storage()->mutable_configure(), input_transaction.configure());
  protobuf_copy_message(*output.mutable_storage()->mutable_transaction_data(), input_transaction.transaction_data());

  protobuf_copy_message(*output.mutable_storage()->mutable_participator_data(), input_participator.participator_data());
}

DISTRIBUTED_TRANSACTION_SDK_API void pack_participator_request(
    atfw::distributed_system::SSParticipatorTransactionCommitReq& output,
    const atfw::distributed_system::transaction_blob_storage& input_transaction,
    const atfw::distributed_system::transaction_participator&) {
  output.set_transaction_uuid(input_transaction.metadata().transaction_uuid());
}

DISTRIBUTED_TRANSACTION_SDK_API void pack_participator_request(
    atfw::distributed_system::SSParticipatorTransactionRejectReq& output,
    const atfw::distributed_system::transaction_blob_storage& input_transaction,
    const atfw::distributed_system::transaction_participator& input_participator) {
  output.set_transaction_uuid(input_transaction.metadata().transaction_uuid());

  if (input_transaction.configure().force_commit()) {
    protobuf_copy_message(*output.mutable_storage()->mutable_metadata(), input_transaction.metadata());
    protobuf_copy_message(*output.mutable_storage()->mutable_configure(), input_transaction.configure());
    protobuf_copy_message(*output.mutable_storage()->mutable_transaction_data(), input_transaction.transaction_data());

    protobuf_copy_message(*output.mutable_storage()->mutable_participator_data(),
                          input_participator.participator_data());
  }
}

}  // namespace transaction_api
}  // namespace rpc
