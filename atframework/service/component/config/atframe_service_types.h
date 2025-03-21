#ifndef ATFRAME_SERVICE_COMPONENT_CONFIG_SERVICE_TYPES_H
#define ATFRAME_SERVICE_COMPONENT_CONFIG_SERVICE_TYPES_H

#pragma once

namespace atframework {
namespace component {
struct service_type {
  enum type {
    EN_ATST_UNKNOWN = 0,
    EN_ATST_ATPROXY,
    EN_ATST_GATEWAY,

    EN_ATST_INNER_BOUND = 0x20,
    EN_ATST_CUSTOM_START = 0x21,
  };
};
}  // namespace component
}  // namespace atframework
#endif
