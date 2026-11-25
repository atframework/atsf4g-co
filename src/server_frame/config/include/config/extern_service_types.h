// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

#include <config/atframe_service_types.h>

#include "config/server_frame_build_feature.h"  // IWYU pragma: keep

namespace atframework {
namespace component {
enum class message_type : uint32_t {
  kInServerMessage = 11,  // solution services
};

}  // namespace component
}  // namespace atframework
