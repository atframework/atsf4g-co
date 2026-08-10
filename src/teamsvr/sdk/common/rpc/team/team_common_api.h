// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <cstdint>

namespace rpc {
namespace team {
namespace team_api {
TEAM_SDK_COMMON_API uint64_t get_teamsvr_server_id_of_zone(uint32_t zone_id, int64_t team_id);
}
}  // namespace team
}  // namespace rpc
