// Copyright 2026 atframework
// Created by owent on 2026-09-20

#pragma once

#include <gsl/select-gsl.h>

#include <transaction_client_handle.h>

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <string>
#include <utility>

namespace rpc {
namespace friend_api {

using atfw::distributed_system::transaction_client_handle;

FRIEND_SDK_MANAGEMENT_API std::string friend_key_to_transaction_participator_key(uint32_t zone_id, uint64_t user_id);
FRIEND_SDK_MANAGEMENT_API std::pair<uint32_t, uint64_t> transaction_participator_key_to_friend_key(
    gsl::string_view key);

FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::vtable_type& get_default_transaction_delegator();
FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::transaction_options get_normal_transaction_options();
FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::transaction_options get_force_commit_transaction_options();

}  // namespace friend_api
}  // namespace rpc
