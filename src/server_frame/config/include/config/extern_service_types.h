#ifndef ATFRAME_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H
#define ATFRAME_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H

#pragma once

#include <config/compile_optimize.h>

#include <config/atframe_service_types.h>

#include "config/server_frame_build_feature.h"

namespace atframework {
namespace component {
enum class message_type : uint32_t {
  kInServerMessage = 11,  // solution services
};

enum class logic_service_type : uint32_t {
  kAuthSvr = 11,
  kLobbySvr = 12,
  kDtCoordSvr = 13,  // 用于分布式事务的协调者服务
};
}  // namespace component
}  // namespace atframework
#endif
