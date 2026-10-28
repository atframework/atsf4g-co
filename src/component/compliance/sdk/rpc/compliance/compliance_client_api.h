// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>
#include <logic/hpa/logic_hpa_easy_api.h>

#include <atframe/etcdcli/etcd_discovery.h>

#include <cstdint>

#include "rpc/compliance/complianceservice.atfw.gen.h"

namespace rpc {
namespace compliance {

/**
 * @brief Get the target server object
 *
 * @param routing_key
 * @param mode
 * @return 按哈希键选择合规服务节点；默认只选已就绪节点，无可用节点时返回 nullptr。
 */
COMPLIANCE_SDK_API atfw::atapp::etcd_discovery_node::ptr_t get_target_server_id(
    gsl::string_view routing_key, logic_hpa_discovery_select_mode mode = logic_hpa_discovery_select_mode::kReady);

/**
 * @brief 敏感词过滤
 *
 * @param ctx 上下文
 * @param request 请求
 * @param response 回包
 * @return 0或错误码的RPC result包装
 */
ATFW_EXPLICIT_NODISCARD_ATTR COMPLIANCE_SDK_API rpc::result_code_type filter_text(
    rpc::context& ctx, atframework::compliance::SSFilterTextReq& request,
    atframework::compliance::SSFilterTextRsp& response);

}  // namespace compliance
}  // namespace rpc
