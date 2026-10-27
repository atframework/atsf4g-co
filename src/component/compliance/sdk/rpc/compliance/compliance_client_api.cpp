// Copyright 2026 atframework

#include "rpc/compliance/compliance_client_api.h"

#include <atframe/etcdcli/etcd_discovery.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.hpa.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>
#include <logic/logic_server_setup.h>

namespace rpc {
namespace compliance {

COMPLIANCE_SDK_API atfw::atapp::etcd_discovery_node::ptr_t get_target_server_id(gsl::string_view routing_key,
                                                                                logic_hpa_discovery_select_mode mode) {
  auto* module = logic_server_last_common_module();
  if (module == nullptr) {
    return nullptr;
  }

  auto discovery =
      module->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kComplianceSvr));
  if (!discovery) {
    return nullptr;
  }

  auto selected = discovery->get_node_hash_by_consistent_hash(
      routing_key, logic_hpa_discovery_select(
                       PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kComplianceSvrFieldNumber, mode));
  return selected.node;
}

COMPLIANCE_SDK_API rpc::result_code_type filter_text(rpc::context& ctx,
                                                     atframework::compliance::SSFilterTextReq& request,
                                                     atframework::compliance::SSFilterTextRsp& response) {
  if (request.items().empty()) {
    response.Clear();
    RPC_RETURN_CODE(0);
  }
  const auto& routing_key = request.items(0).text();
  atfw::atapp::etcd_discovery_node::ptr_t target_server_node =
      get_target_server_id(gsl::string_view{routing_key.data(), routing_key.size()});
  if (!target_server_node) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ATBUS_ERR_ATNODE_NOT_FOUND);
  }

  rpc::result_code_type::value_type result =
      RPC_AWAIT_CODE_RESULT(rpc::compliance::unicast::filter_text(ctx, *target_server_node, request, response));
  RPC_RETURN_CODE(result);
}

}  // namespace compliance
}  // namespace rpc
