// Copyright 2026 atframework

#include "logic/item/user_item_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.user.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <config/excel/config_manager.h>

#include <data/user.h>

#include <algorithm>
#include <map>

user_item_manager::user_item_manager(user& owner) : owner_(&owner) {}