// Copyright 2021 atframework
// Created by owent

#pragma once

#include <algorithm/murmur_hash.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

struct player_key_hash_t {
  size_t operator()(const PROJECT_NAMESPACE_ID::DPlayerIDKey& key) const {
    uint64_t out[2] = {0};
    uint64_t val = key.user_id();
    atfw::util::hash::murmur_hash3_x64_128(&val, static_cast<int>(sizeof(val)), key.zone_id(), out);
    return out[0];
  }
};

struct player_key_equal_t {
  bool operator()(const PROJECT_NAMESPACE_ID::DPlayerIDKey& l, const PROJECT_NAMESPACE_ID::DPlayerIDKey& r) const {
    return l.zone_id() == r.zone_id() && l.user_id() == r.user_id();
  }
};
