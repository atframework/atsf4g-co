#ifndef ATFRAME_SERVICE_COMPONENT_CONFIG_SERVICE_TYPES_H
#define ATFRAME_SERVICE_COMPONENT_CONFIG_SERVICE_TYPES_H

#pragma once

#include <cstdint>

namespace atframework {
namespace component {
enum class service_type : uint32_t {
  kUnknown = 0,
  kAtProxy = 1,
  kAtGateway = 2,

  kInternal = 10,
  kCustomStart = 10,
};
}  // namespace component
}  // namespace atframework
#endif
