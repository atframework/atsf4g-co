// Copyright 2024 atframework
// Created by owent

// Windows头文件需要前置include，不然会有冲突
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

#  include <WinSock2.h>
#endif

#include "logic/hpa/logic_hpa_easy_api.h"

#include <config/compile_optimize.h>

#include <atframe/atapp_conf.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include <config/logic_config.h>

#include <cstdint>
#include <type_traits>
#include <unordered_map>

#include "logic/hpa/logic_hpa_controller.h"
#include "logic/logic_server_setup.h"

#ifdef GetMessage
#  undef GetMessage
#endif

namespace {
static void rebuild_enabled_services_cache(
    std::unordered_map<int32_t, const google::protobuf::FieldDescriptor*>& enabled_services) {
  auto descriptor = PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::descriptor();
  enabled_services.clear();
  enabled_services.reserve(static_cast<std::size_t>(descriptor->field_count()));

  for (int i = 0; i < descriptor->field_count(); ++i) {
    auto fds = descriptor->field(i);
    if (fds == nullptr) {
      continue;
    }

    // 检查类型，以防止错误的协议配置
    if (fds->is_repeated()) {
      FWLOGERROR("[HPA]: Easy API -> {} is repeated field and is a invalid HPA discovery service.", fds->full_name());
      continue;
    }
    if (fds->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE || fds->message_type() == nullptr) {
      FWLOGERROR("[HPA]: Easy API -> {} (with type {}) is a invalid HPA discovery service.", fds->full_name(),
                 fds->type_name());
      continue;
    }
    if (fds->message_type() != atapp::protocol::atapp_metadata::descriptor()) {
      FWLOGERROR(
          "[HPA]: Easy API -> {} (except message type {}, real message type {}) is a invalid HPA discovery "
          "service.",
          atapp::protocol::atapp_metadata::descriptor()->full_name(), fds->full_name(),
          fds->message_type()->full_name());
      continue;
    }

    enabled_services[fds->number()] = fds;
  }
}

const atapp::protocol::atapp_metadata* find_enabled_services_cache(int32_t type_id) {
  const PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg& origin_cfg =
      logic_config::me()->get_server_cfg().logic().discovery_selector();

  auto fds = origin_cfg.GetDescriptor()->FindFieldByNumber(type_id);
  if (nullptr == fds) {
    return nullptr;
  }

  // 检查类型，以防止错误的协议配置
  if (fds->is_repeated() || fds->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    return nullptr;
  }
  if (fds->message_type() != atapp::protocol::atapp_metadata::descriptor()) {
    return nullptr;
  }

  if (!origin_cfg.GetReflection()->HasField(origin_cfg, fds)) {
    return nullptr;
  }

  return static_cast<const atapp::protocol::atapp_metadata*>(&origin_cfg.GetReflection()->GetMessage(origin_cfg, fds));
}
}  // namespace

SERVER_FRAME_API const atapp::protocol::atapp_metadata* logic_hpa_discovery_select(
    int32_t type_id, logic_hpa_discovery_select_mode mode) noexcept {
  static std::unordered_map<int32_t, const google::protobuf::FieldDescriptor*> enabled_services;
  static int64_t configure_version[2] = {0, 0};

  auto& reload_time = logic_config::me()->get_server_cfg().logic().server().reload_timepoint();
  if (reload_time.seconds() != configure_version[0] || reload_time.nanos() != configure_version[1]) {
    configure_version[0] = reload_time.seconds();
    configure_version[1] = reload_time.nanos();
    rebuild_enabled_services_cache(enabled_services);
  }

  auto fds_iter = enabled_services.find(type_id);

  // 如果没配置，走非HPA控制的discovery配置
  if (enabled_services.end() == fds_iter) {
    return find_enabled_services_cache(type_id);
  }

  static_assert(
      std::is_same<decltype(logic_config::me()->get_server_cfg().logic().hpa().discovery().scaling_ready()),
                   decltype(logic_config::me()->get_server_cfg().logic().hpa().discovery().scaling_target())>::value,
      "Type checking - 1");
  static_assert(std::is_same<decltype(logic_config::me()->get_server_cfg().logic().hpa().discovery().scaling_ready()),
                             decltype(logic_config::me()->get_server_cfg().logic().discovery_selector())>::value,
                "Type checking - 2");

  const PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg* use_cfg = nullptr;
  switch (mode) {
    case logic_hpa_discovery_select_mode::kTarget:
      use_cfg = &logic_config::me()->get_server_cfg().logic().hpa().discovery().scaling_target();
      break;
    default:
      use_cfg = &logic_config::me()->get_server_cfg().logic().hpa().discovery().scaling_ready();
      break;
  }

  if (!use_cfg->GetReflection()->HasField(*use_cfg, fds_iter->second)) {
    return nullptr;
  }

  return static_cast<const atapp::protocol::atapp_metadata*>(
      &use_cfg->GetReflection()->GetMessage(*use_cfg, fds_iter->second));
}

SERVER_FRAME_API bool logic_hpa_current_node_is_ready() noexcept {
  auto mod = logic_server_last_common_module();
  if (nullptr == mod) {
    return false;
  }

  if (!mod->get_hpa_controller()) {
    return false;
  }

  return mod->get_hpa_controller()->get_discovery_ready_tag();
}

SERVER_FRAME_API bool logic_hpa_current_node_is_in_target() noexcept {
  auto mod = logic_server_last_common_module();
  if (nullptr == mod) {
    return false;
  }

  if (!mod->get_hpa_controller()) {
    return false;
  }

  return mod->get_hpa_controller()->get_discovery_target_tag();
}
