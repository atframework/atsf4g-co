// Copyright 2021 atframework
// Created by owent on 2016/9/23.
//

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/extension/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <config/ini_loader.h>
#include <design_pattern/singleton.h>
#include <memory/rc_ptr.h>

#include <atframe/atapp_config.h>

#include <config/server_frame_build_feature.h>

#include <string>

LIBATAPP_MACRO_NAMESPACE_BEGIN
class app;
LIBATAPP_MACRO_NAMESPACE_END

class logic_config {
 private:
  SERVER_FRAME_CONFIG_API logic_config();
  SERVER_FRAME_CONFIG_API ~logic_config();

#if defined(SERVER_FRAME_CONFIG_DLL) && SERVER_FRAME_CONFIG_DLL
#  if defined(SERVER_FRAME_CONFIG_NATIVE) && SERVER_FRAME_CONFIG_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(logic_config)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(logic_config)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(logic_config)
#endif

 public:
  using custom_config_loader_func = void (*)(atfw::atapp::app &, logic_config &);

  SERVER_FRAME_CONFIG_API int init(uint64_t server_id, const std::string &server_name);

  SERVER_FRAME_CONFIG_API int reload(atfw::atapp::app &app);

  SERVER_FRAME_CONFIG_API uint64_t get_local_server_id() const noexcept;
  SERVER_FRAME_CONFIG_API uint32_t get_local_zone_id() const noexcept;
  SERVER_FRAME_CONFIG_API gsl::string_view get_local_server_name() const noexcept;
  SERVER_FRAME_CONFIG_API gsl::string_view get_local_server_id_readable() const noexcept;

  SERVER_FRAME_CONFIG_API gsl::string_view get_deployment_environment_name() const noexcept;

  SERVER_FRAME_CONFIG_API const PROJECT_NAMESPACE_ID::DConstSettingsType &get_const_settings();
  SERVER_FRAME_CONFIG_API const atframework::ConstSettingsType &get_atframework_settings();

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_section_cfg &get_server_cfg() const noexcept {
    return server_cfg_;
  }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::config::logic_section_cfg *mutable_server_cfg() noexcept {
    return &server_cfg_;
  }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_db_cfg &get_cfg_db() const noexcept {
    return get_server_cfg().db();
  }
  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &get_cfg_telemetry() const noexcept {
    return get_server_cfg().telemetry();
  }
  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_router_cfg &get_cfg_router() const noexcept {
    return get_server_cfg().router();
  }
  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_task_cfg &get_cfg_task() const noexcept {
    return get_server_cfg().task();
  }

  ATFW_UTIL_FORCEINLINE void set_custom_config_loader(custom_config_loader_func loader) noexcept {
    custom_config_loader_ = loader;
  }

  template <typename CustomConfigProtocol>
  SERVER_FRAME_CONFIG_API const CustomConfigProtocol &get_custom_config() const noexcept {
    static CustomConfigProtocol empty;
    if (!custom_config_) {
      return empty;
    }
    auto config_ptr = dynamic_cast<const CustomConfigProtocol *>(custom_config_.get());
    if (config_ptr) {
      return *config_ptr;
    }
    return empty;
  }

  SERVER_FRAME_CONFIG_API atfw::util::memory::strong_rc_ptr<google::protobuf::Message> &
  mutable_custom_config() noexcept {
    return custom_config_;
  }

 private:
  void _load_db();
  void _load_db_hosts(PROJECT_NAMESPACE_ID::config::db_group_cfg &out);

  void _load_server_cfg(atfw::atapp::app &app);

 private:
  const PROJECT_NAMESPACE_ID::DConstSettingsType *const_settings_;
  const atframework::ConstSettingsType *atframe_settings_;

  custom_config_loader_func custom_config_loader_;
  atfw::util::memory::strong_rc_ptr<google::protobuf::Message> custom_config_;

  PROJECT_NAMESPACE_ID::config::logic_section_cfg server_cfg_;
  std::string readable_app_id_;
};
