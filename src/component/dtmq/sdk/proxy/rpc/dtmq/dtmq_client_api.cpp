// Copyright 2026 atframework

#include "rpc/dtmq/dtmq_client_api.h"

#include <algorithm/murmur_hash.h>

#include <gsl/select-gsl.h>

#include <atframe/etcdcli/etcd_discovery.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/svr.protocol.config.pb.h>
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
#include "config/compile_optimize.h"
#include "protocol/pbdesc/com.const.pb.h"

namespace rpc {
namespace dtmq {

namespace {
constexpr const uint32_t kDtmqProxysvrReplicationHashCode = 0x5f3759df;
struct ATFW_UTIL_SYMBOL_LOCAL dtmq_proxysvr_replication_hash_combine {
  uint64_t replicate_index;
  uint64_t channel_key_hash[2];
};
static_assert(sizeof(dtmq_proxysvr_replication_hash_combine) == sizeof(uint64_t) * 3,
              "dtmq_proxysvr_replication_hash_combine size mismatch");
}  // namespace

DTMQ_PROXY_SDK_API uint64_t get_target_server_id(const atfw::dtmq::DChannelIdKey& channel_key, replicate_type status,
                                                 size_t replicate_index, logic_hpa_discovery_select_mode mode) {
  if (channel_key.channel_id().empty()) {
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

  dtmq_proxysvr_replication_hash_combine combine_hash{};
  if (status == rpc::dtmq::replicate_type::kWritable) {
    combine_hash.replicate_index = 0;
  } else {
    combine_hash.replicate_index = static_cast<uint64_t>(replicate_index);
  }

  atfw::util::hash::murmur_hash3_x64_128(channel_key.channel_id().data(),
                                         static_cast<int>(channel_key.channel_id().size()),
                                         kDtmqProxysvrReplicationHashCode, combine_hash.channel_key_hash);

  atapp::etcd_discovery_set::node_hash_type node_hash;
  node_hash = discovery_set->get_node_hash_by_consistent_hash(
      gsl::make_span(reinterpret_cast<const unsigned char*>(&combine_hash), sizeof(combine_hash)),
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kDtmqProxysvrFieldNumber,
                                 mode));
  if (!node_hash.node) {
    return 0;
  }

  return node_hash.node->get_discovery_info().id();
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
    rpc::context& ctx, atfw::dtmq::channel_subscriber&& sender_info, atfw::dtmq::DChannelIdKey& channel_key,
    atfw::dtmq::DChannelMessageDetail&& detail,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr, bool auto_create_channel,
    bool no_wait) {
  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  uint64_t target_server_id = get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable);
  if (0 == target_server_id) {
    FWLOGDEBUG("get_target_server_id target_server_id is zero. channel_id:({})", channel_key.channel_id());
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
  rpc_req_body->mutable_message_content()->set_channel_type(channel_key.channel_type());

  auto ret =
      RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, target_server_id, *rpc_req_body, *rpc_rsp_body, no_wait));

  if (rpc_rsp_body->client_result() != 0) {
    RPC_RETURN_CODE(rpc_rsp_body->client_result());
  }

  if (compare_and_maybe_reset_lock_rsp_ptr) {
    protobuf_copy_message(*compare_and_maybe_reset_lock_rsp_ptr, rpc_rsp_body->compare_and_maybe_reset_lock());
  }

  RPC_RETURN_CODE(ret);
}

DTMQ_PROXY_SDK_API rpc::result_code_type find_message(rpc::context& ctx, atfw::dtmq::DChannelIdKey& channel_key,
                                                      int64_t sequence, atfw::dtmq::DChannelMessage& msg) {
  rpc::context::message_holder<atfw::dtmq::SSChannelFindMessageReq> rpc_req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelFindMessageRsp> rpc_rsp_body{ctx};

  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  uint64_t target_server_id = get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable);
  if (0 == target_server_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  protobuf_copy_message(*rpc_req_body->mutable_channel_key(), channel_key);
  rpc_req_body->set_sequence(sequence);

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::find_message(ctx, target_server_id, *rpc_req_body, *rpc_rsp_body));
  protobuf_copy_message(msg, rpc_rsp_body->channel_message());

  if (rpc_rsp_body->client_result() < 0) {
    RPC_RETURN_CODE(rpc_rsp_body->client_result());
  }

  RPC_RETURN_CODE(ret);
}

DTMQ_PROXY_SDK_API rpc::result_code_type page_query_message(
    rpc::context& ctx, atfw::dtmq::DChannelIdKey& channel_key, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs) {
  rpc::context::message_holder<atfw::dtmq::SSChannelQueryMessageReq> rpc_req_body{ctx};
  rpc::context::message_holder<atfw::dtmq::SSChannelQueryMessageRsp> rpc_rsp_body{ctx};

  if (channel_key.channel_id().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  uint64_t target_server_id = get_target_server_id(channel_key, rpc::dtmq::replicate_type::kWritable);
  if (0 == target_server_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  protobuf_copy_message(*rpc_req_body->mutable_channel_key(), channel_key);
  protobuf_copy_message(*rpc_req_body->mutable_page_info(), page_info);

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::page_query_message(ctx, target_server_id, *rpc_req_body, *rpc_rsp_body));
  protobuf_copy_message(page_info, rpc_rsp_body->page_info());
  protobuf_copy_message(msgs, rpc_rsp_body->channel_message());

  if (rpc_rsp_body->client_result() < 0) {
    RPC_RETURN_CODE(rpc_rsp_body->client_result());
  }
  RPC_RETURN_CODE(ret);
}

}  // namespace dtmq
}  // namespace rpc
