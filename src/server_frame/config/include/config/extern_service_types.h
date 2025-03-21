#ifndef ATFRAME_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H
#define ATFRAME_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H

#pragma once

#include <config/compile_optimize.h>

#include <config/atframe_service_types.h>

#include "config/server_frame_build_feature.h"

namespace atframework {
namespace component {
struct SERVER_FRAME_CONFIG_HEAD_ONLY message_type {
  enum type {
    EN_ATST_SS_MSG = service_type::EN_ATST_CUSTOM_START,  // solution services
  };
};

struct SERVER_FRAME_CONFIG_HEAD_ONLY logic_service_type {
  enum type {
    EN_LST_LOGINSVR = 35,
    EN_LST_GAMESVR = 36,
    EN_LST_DTCOORDSVR = 70,  // 用于分布式事务的协调者服务
  };
};
}  // namespace component
}  // namespace atframework
#endif
