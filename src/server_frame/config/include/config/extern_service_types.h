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
  kDtCoordSvr = 13,          // 用于分布式事务的协调者服务
  kCacheSvr = 15,            // 缓存服务器
  kRankSettlementSvr = 16,   // 排行榜结算服务器
  kRankBoardSvr = 17,        // 排行榜展示服务器
  kOrbitSvr = 18,            // Orbit 服务

  // === 保留一部分21-25，以后消息队列服务精细化服务角色 ===
  kDtMqMetaSvr = 21,  // 分布式消息队列(协调)服务

  // Orbit 相关服务 === 保留一部分101-105
  kOrbitAgentSvr = 101,       // Orbit Agent 服务
  kOrbitControllerSvr = 102,  // Orbit Controller 服务
};
}  // namespace component
}  // namespace atframework
#endif
