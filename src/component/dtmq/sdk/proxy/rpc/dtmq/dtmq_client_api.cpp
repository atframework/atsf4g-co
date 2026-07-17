// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/dtmq/dtmq_client_api.h"

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <atframe/etcdcli/etcd_discovery.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/dtmq/dtmqproxysvrservice.atfw.gen.h>
#include <rpc/rpc_context.h>

#include <config/extern_service_types.h>
#include <config/server_frame_build_feature.h>

#include <logic/logic_server_setup.h>
#include <utility/protobuf_mini_dumper.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rpc {
namespace dtmq {

namespace {
// 单个频道副本数量硬限制
constexpr const uint64_t kDtmqProxySvrMaxReplicateIndex = 65536;
}  // namespace

DTMQ_PROXY_SDK_API uint64_t get_target_server_id(const atfw::dtmq::DChannelIdKey& channel_key, replicate_type status,
                                                 uint64_t replicate_index, logic_hpa_discovery_select_mode mode) {
  if (channel_key.channel_id().empty()) {
    return 0;
  }

  if (replicate_index > kDtmqProxySvrMaxReplicateIndex) {
    return 0;
  }

  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return 0;
  }

  auto discovery_set =
      mod->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kDtMqProxySvr));
  if (!discovery_set) {
    return 0;
  }

  atapp::etcd_discovery_set::node_hash_type node_hash;
  node_hash = discovery_set->get_node_hash_by_consistent_hash(
      channel_key.channel_id(),
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kDtmqProxysvrFieldNumber,
                                 mode));
  if (!node_hash.node) {
    return 0;
  }

  if (status == rpc::dtmq::replicate_type::kWritable || 0 == replicate_index) {
    return node_hash.node->get_discovery_info().id();
  }

  // 只读副本使用lower_bound来选择，这如果writable节点故障，按照一致性hash的原理能够尽可能转移到只读副本，尽可能保留数据。
  std::vector<atapp::etcd_discovery_set::node_hash_type> output;
  output.resize(static_cast<size_t>(replicate_index + 1));
  discovery_set->lower_bound_node_hash_by_consistent_hash(gsl::make_span(output), node_hash);

  if ((*output.rbegin()).node) {
    return (*output.rbegin()).node->get_discovery_info().id();
  }

  return 0;
}

DTMQ_PROXY_SDK_API void get_target_server_ids(std::vector<uint64_t>& server_ids,
                                              const atfw::dtmq::DChannelIdKey& channel_key,
                                              uint64_t replicate_index_count, logic_hpa_discovery_select_mode mode) {
  server_ids.clear();
  if (channel_key.channel_id().empty()) {
    return;
  }

  if (replicate_index_count > kDtmqProxySvrMaxReplicateIndex) {
    replicate_index_count = kDtmqProxySvrMaxReplicateIndex;
  }

  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return;
  }

  auto discovery_set =
      mod->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kDtMqProxySvr));
  if (!discovery_set) {
    return;
  }

  server_ids.reserve(static_cast<size_t>(replicate_index_count + 1));

  atapp::etcd_discovery_set::node_hash_type node_hash;
  node_hash = discovery_set->get_node_hash_by_consistent_hash(
      channel_key.channel_id(),
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kDtmqProxysvrFieldNumber,
                                 mode));
  if (node_hash.node) {
    server_ids.emplace_back(node_hash.node->get_discovery_info().id());
  } else {
    server_ids.emplace_back(0);
  }

  if (0 == replicate_index_count) {
    return;
  }

  // 只读副本使用lower_bound来选择，这如果writable节点故障，按照一致性hash的原理能够尽可能转移到只读副本，尽可能保留数据。
  std::vector<atapp::etcd_discovery_set::node_hash_type> output;
  output.resize(static_cast<size_t>(replicate_index_count + 1));
  discovery_set->lower_bound_node_hash_by_consistent_hash(gsl::make_span(output), node_hash);

  for (size_t i = 1; i < output.size(); ++i) {
    if (output[i].node) {
      server_ids.emplace_back(output[i].node->get_discovery_info().id());
    } else {
      server_ids.emplace_back(0);
    }
  }
}

DTMQ_PROXY_SDK_API bool has_dtmq_proxysvr() {
  auto* mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return false;
  }

  auto discovery_set =
      mod->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kDtMqProxySvr));
  if (!discovery_set) {
    return false;
  }

  return !discovery_set
              ->get_sorted_nodes(logic_hpa_discovery_select(
                  PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kDtmqProxysvrFieldNumber,
                  logic_hpa_discovery_select_mode::kReady))
              .empty();
}

DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
    rpc::context& ctx, atfw::dtmq::channel_subscriber&& sender_info, const atfw::dtmq::DChannelIdKey& channel_key,
    atfw::dtmq::DChannelMessageDetail&& detail,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr, bool auto_create_channel,
    bool no_wait) {
  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  uint64_t target_server_id = get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable);
  if (0 == target_server_id) {
    FWLOGDEBUG("No server available for channel_id:({})", channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  rpc::context::message_holder<atfw::dtmq::SSChannelSendMessageReq> rpc_req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelSendMessageRsp> rpc_rsp_body{ctx};

  if (compare_and_maybe_reset_lock_ptr) {
    protobuf_copy_message(*rpc_req_body->mutable_compare_and_maybe_reset_lock(), *compare_and_maybe_reset_lock_ptr);
  }
  rpc_req_body->set_auto_create_channel(auto_create_channel);

  protobuf_copy_message(*rpc_req_body->mutable_channel_key(), channel_key);
  protobuf_move_message(*rpc_req_body->mutable_message_content()->mutable_detail(), std::move(detail));
  *rpc_req_body->mutable_message_content()->mutable_sender_key() = sender_info.subscriber_key();
  rpc_req_body->set_subscriber_last_hash_code(sender_info.last_heartbeat_hash_code());
  rpc_req_body->set_subscriber_last_sequence(sender_info.last_heartbeat_sequence());
  protobuf_move_message(*rpc_req_body->mutable_subscriber(), std::move(sender_info));
  rpc_req_body->mutable_message_content()->set_channel_type(channel_key.channel_type());

  auto ret =
      RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, target_server_id, *rpc_req_body, *rpc_rsp_body, no_wait));

  if (compare_and_maybe_reset_lock_rsp_ptr && rpc_rsp_body->has_compare_and_maybe_reset_lock()) {
    protobuf_copy_message(*compare_and_maybe_reset_lock_rsp_ptr, rpc_rsp_body->compare_and_maybe_reset_lock());
  }

  if (rpc_rsp_body->client_result() != 0) {
    RPC_RETURN_CODE(rpc_rsp_body->client_result());
  }

  RPC_RETURN_CODE(ret);
}

DTMQ_PROXY_SDK_API rpc::result_code_type find_message(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
                                                      int64_t sequence, atfw::dtmq::DChannelMessage& msg) {
  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  uint64_t target_server_id = get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable);
  if (0 == target_server_id) {
    FWLOGDEBUG("No server available for channel_id:({})", channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  rpc::context::message_holder<atfw::dtmq::SSChannelFindMessageReq> rpc_req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelFindMessageRsp> rpc_rsp_body{ctx};

  protobuf_copy_message(*rpc_req_body->mutable_channel_key(), channel_key);
  rpc_req_body->set_sequence(sequence);

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::find_message(ctx, target_server_id, *rpc_req_body, *rpc_rsp_body));

  // 尽可能保留数据，无视错误，收到的数据一律传出
  protobuf_copy_message(msg, rpc_rsp_body->channel_message());

  if (rpc_rsp_body->client_result() < 0) {
    RPC_RETURN_CODE(rpc_rsp_body->client_result());
  }

  RPC_RETURN_CODE(ret);
}

DTMQ_PROXY_SDK_API rpc::result_code_type page_query_message(
    rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  uint64_t target_server_id = get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable);
  if (0 == target_server_id) {
    FWLOGDEBUG("No server available for channel_id:({})", channel_key.channel_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  rpc::context::message_holder<atfw::dtmq::SSChannelQueryMessageReq> rpc_req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelQueryMessageRsp> rpc_rsp_body{ctx};

  protobuf_copy_message(*rpc_req_body->mutable_channel_key(), channel_key);
  protobuf_copy_message(*rpc_req_body->mutable_page_info(), page_info);

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::page_query_message(ctx, target_server_id, *rpc_req_body, *rpc_rsp_body));

  // 尽可能保留数据，无视错误，收到的数据一律传出
  protobuf_copy_message(page_info, rpc_rsp_body->page_info());
  protobuf_copy_message(msgs, rpc_rsp_body->channel_message());

  if (rpc_rsp_body->client_result() < 0) {
    RPC_RETURN_CODE(rpc_rsp_body->client_result());
  }
  RPC_RETURN_CODE(ret);
}

}  // namespace dtmq
}  // namespace rpc
