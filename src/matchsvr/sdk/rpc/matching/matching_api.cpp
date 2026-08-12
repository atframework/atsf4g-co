// Copyright 2026 atframework

#include "rpc/matching/matching_api.h"

#include <config/extern_service_types.h>
#include <logic/hpa/logic_hpa_easy_api.h>
#include <logic/logic_server_setup.h>

namespace rpc {
namespace matching_api {

uint64_t get_matchsvr_server_id() {
  auto* module = logic_server_last_common_module();
  if (module == nullptr) {
    return 0;
  }
  auto discovery =
      module->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kMatchSvr));
  if (!discovery) {
    return 0;
  }
  auto nodes = discovery->get_sorted_nodes(
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kMatchsvrFieldNumber,
                                 logic_hpa_discovery_select_mode::kReady));
  return nodes.empty() || !nodes.front() ? 0 : nodes.front()->get_discovery_info().id();
}

}  // namespace matching_api
}  // namespace rpc
