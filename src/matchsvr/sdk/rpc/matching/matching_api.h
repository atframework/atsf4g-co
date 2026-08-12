// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstdint>

namespace rpc {
namespace matching_api {

// 选择当前唯一的 ready matchsvr。保留集中入口，未来扩容时可替换为一致性路由。
GAME_RPC_API uint64_t get_matchsvr_server_id();

}  // namespace matching_api
}  // namespace rpc
