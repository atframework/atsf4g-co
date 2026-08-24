// Copyright 2026 atframework
// Created by owent

#pragma once

#include <algorithm/murmur_hash.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.team.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <cstddef>
#include <cstdint>

namespace rpc {
namespace team {
namespace team_api {

// DTeamKey 的容器适配: 以 (zone_id, team_id) 为队伍唯一标识，
// 可直接作为 std::unordered_map 的 hash key(team_key_hash_t + team_key_equal_t)
// 或 std::map/std::set 的比较器(team_key_less_t)

struct ATFW_UTIL_SYMBOL_VISIBLE team_key_hash_t {
  size_t operator()(const atfw::team::DTeamKey& key) const {
    uint64_t out[2] = {0};
    uint64_t val = static_cast<uint64_t>(key.team_id());
    atfw::util::hash::murmur_hash3_x64_128(&val, static_cast<int>(sizeof(val)), key.zone_id(), out);
    return out[0];
  }
};

struct ATFW_UTIL_SYMBOL_VISIBLE team_key_equal_t {
  bool operator()(const atfw::team::DTeamKey& l, const atfw::team::DTeamKey& r) const {
    return l.zone_id() == r.zone_id() && l.team_id() == r.team_id();
  }
};

struct ATFW_UTIL_SYMBOL_VISIBLE team_key_less_t {
  bool operator()(const atfw::team::DTeamKey& l, const atfw::team::DTeamKey& r) const {
    return l.zone_id() < r.zone_id() || (l.zone_id() == r.zone_id() && l.team_id() < r.team_id());
  }
};

}  // namespace team_api
}  // namespace team
}  // namespace rpc
