// Copyright 2026 atframework
// Created by owent on 2026-09-28

#pragma once

#include <memory/rc_ptr.h>

#include <config/server_frame_build_feature.h>

#include <transaction_client_handle.h>

#include <data/user_key_hash_helper.h>

#include "logic/friend_api/friend_api_defs.h"  // IWYU pragma: keep

using friend_api_transaction_client_handle = atframework::distributed_system::transaction_client_handle;

class user_friend_api_manager;

namespace rpc {
class context;
}

atfw::util::memory::strong_rc_ptr<friend_api_transaction_client_handle> user_friend_api_create_transaction_client(
    user_friend_api_manager&);
