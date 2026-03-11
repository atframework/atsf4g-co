#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>
#include <utility/persistent_btree.h>

using rank_tree = persistent_btree<PROJECT_NAMESPACE_ID::rank_sort_data>;
using rank_mirror = rank_tree::mirror_type;
