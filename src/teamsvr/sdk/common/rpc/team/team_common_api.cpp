// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/team/team_common_api.h"

#include <config/extern_service_types.h>
#include <logic/hpa/logic_hpa_easy_api.h>
#include <logic/logic_server_setup.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.hpa.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace rpc {
namespace team {
namespace team_api {
TEAM_SDK_COMMON_API uint64_t get_teamsvr_room_server_id_of_zone(uint32_t zone_id, int64_t team_id) {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return 0;
  }

  auto discovery_set = mod->get_discovery_index_by_type_zone(
      static_cast<uint64_t>(atfw::component::logic_service_type::kTeamRoomSvr), static_cast<uint64_t>(zone_id));
  if (!discovery_set) {
    return 0;
  }

  auto node = discovery_set->get_node_hash_by_consistent_hash(
      team_id,
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kTeamsvrFieldNumber,
                                 logic_hpa_discovery_select_mode::kReady));
  if (!node.node) {
    return 0;
  }

  return node.node->get_discovery_info().id();
}

TEAM_SDK_COMMON_API bool has_teamsvr_room() {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return false;
  }

  auto discovery_set =
      mod->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kTeamRoomSvr));
  if (!discovery_set) {
    return false;
  }

  return !discovery_set
              ->get_sorted_nodes(logic_hpa_discovery_select(
                  PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kTeamsvrFieldNumber,
                  logic_hpa_discovery_select_mode::kReady))
              .empty();
}
}  // namespace team_api
}  // namespace team
}  // namespace rpc
