// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>
#include <logic/hpa/logic_hpa_easy_api.h>

#include <atframe/etcdcli/etcd_discovery.h>

#include <cstdint>

#include "rpc/compliance/complianceservice.atfw.gen.h"

namespace rpc {
namespace compliance {

// 按哈希键选择合规服务节点；默认只选已就绪节点，无可用节点时返回 0。
COMPLIANCE_SDK_API atfw::atapp::etcd_discovery_node::ptr_t get_target_server_id(
    gsl::string_view routing_key, logic_hpa_discovery_select_mode mode = logic_hpa_discovery_select_mode::kReady);

// 用首个条目的文本做一致性哈希选点，整个批次只发一次 RPC；空批次直接成功。
// 空文本条目正常发送；失败时不主动清空 response。
// 无可用节点时返回 EN_ATBUS_ERR_ATNODE_NOT_FOUND，远端错误码原样返回。
ATFW_EXPLICIT_NODISCARD_ATTR COMPLIANCE_SDK_API rpc::result_code_type filter_text(
    rpc::context& ctx, atframework::compliance::SSFilterTextReq& request,
    atframework::compliance::SSFilterTextRsp& response);

}  // namespace compliance
}  // namespace rpc
