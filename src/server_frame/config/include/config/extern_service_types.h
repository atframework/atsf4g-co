#ifndef ATFRAMEWORK_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H
#  define ATFRAMEWORK_SERVICE_COMPONENT_ .hCONFIG_EXTERN_SERVICE_TYPES_H

#  pragma once

#  include <config/compile_optimize.h>

#  include <config/atframe_service_types.h>

#  include "config/server_frame_build_feature.h"

namespace atframework {
namespace component {
enum class message_type : uint32_t {
  kInServerMessage = 11,  // solution services
};

enum class logic_service_type : uint32_t {
  kAuthSvr = 11,
  kLobbySvr = 12,
  kDtCoordSvr = 13,         // 用于分布式事务的协调者服务
  kCacheSvr = 15,           // 缓存服务器
  kRankSettlementSvr = 16,  // 排行榜结算服务器
  kRankBoardSvr = 17,       // 排行榜展示服务器
  kOrbitAgentSvr = 18,       // Orbit Agent 服务
  kOrbitControllerSvr = 19,  // Orbit Controller 服务
};
}  // namespace component
}  // namespace atframework
#endif
