// Copyright 2024 atframework
// Created by owent

#pragma once

#include <cstdint>

#include <config/server_frame_build_feature.h>

namespace atapp {
namespace protocol {
class atapp_metadata;
}
}  // namespace atapp

enum class logic_hpa_discovery_select_mode : int32_t {
  // kReady should be used by all client and
  kReady = 0,

  // kTarget is used for services to migrate data before shutdown
  kTarget = 1,
};

/**
 * @brief Get discovery meta of specified service
 *
 * @param type_id Refer to field number of PROJECT_NAMESPACE_ID.config.logic_discovery_selector_cfg
 * @param mode All caller should use kReady, and services need migrate data can use kTarget to get target distribution
 * @return const atapp::protocol::atapp_metadata*
 */
SERVER_FRAME_API const atapp::protocol::atapp_metadata* logic_hpa_discovery_select(
    int32_t type_id, logic_hpa_discovery_select_mode mode = logic_hpa_discovery_select_mode::kReady) noexcept;

/**
 * @brief Get if current is ready
 *
 * @return true if it's ready
 */
SERVER_FRAME_API bool logic_hpa_current_node_is_ready() noexcept;

/**
 * @brief Get if current node is in target distribution
 *
 * @return true if it's in target distribution
 */
SERVER_FRAME_API bool logic_hpa_current_node_is_in_target() noexcept;
