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
#include <nostd/type_traits.h>

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
  using server_instance_config_ptr = atfw::util::memory::strong_rc_ptr<google::protobuf::Message>;
  using server_instance_config_loader_func = void (*)(atfw::atapp::app &, logic_config &, server_instance_config_ptr &);

  SERVER_FRAME_CONFIG_API int init(uint64_t server_id, const std::string &server_name);

  SERVER_FRAME_CONFIG_API int reload(atfw::atapp::app &app);

  SERVER_FRAME_CONFIG_API uint64_t get_local_server_id() const noexcept;
  SERVER_FRAME_CONFIG_API uint32_t get_local_zone_id() const noexcept;
  SERVER_FRAME_CONFIG_API gsl::string_view get_local_server_name() const noexcept;
  SERVER_FRAME_CONFIG_API gsl::string_view get_local_server_id_readable() const noexcept;

  SERVER_FRAME_CONFIG_API gsl::string_view get_deployment_environment_name() const noexcept;

  SERVER_FRAME_CONFIG_API uint32_t get_local_world_id() const noexcept;

  SERVER_FRAME_CONFIG_API const PROJECT_NAMESPACE_ID::DConstSettingsType &get_const_settings();
  SERVER_FRAME_CONFIG_API const atframework::ConstSettingsType &get_atframework_settings();

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_section_cfg &get_logic_cfg() const noexcept {
    return logic_cfg_;
  }
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::config::logic_section_cfg *mutable_logic_cfg() noexcept {
    return &logic_cfg_;
  }

  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_db_cfg &get_cfg_db() const noexcept {
    return get_logic_cfg().db();
  }
  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &get_cfg_telemetry() const noexcept {
    return get_logic_cfg().telemetry();
  }
  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_router_cfg &get_cfg_router() const noexcept {
    return get_logic_cfg().router();
  }
  ATFW_UTIL_FORCEINLINE const PROJECT_NAMESPACE_ID::config::logic_task_cfg &get_cfg_task() const noexcept {
    return get_logic_cfg().task();
  }

  ATFW_UTIL_FORCEINLINE void set_server_instance_config_loader(server_instance_config_loader_func loader) noexcept {
    server_instance_config_loader_ = loader;
  }

  template <class CustomConfigProtocol, class = atfw::util::nostd::enable_if_t<
                                            std::is_base_of<google::protobuf::Message, CustomConfigProtocol>::value>>
  ATFW_UTIL_SYMBOL_VISIBLE const CustomConfigProtocol &get_server_instance_config() const noexcept {
    if (!server_instance_config_data_) {
      return CustomConfigProtocol::default_instance();
    }

    if (server_instance_config_data_->GetDescriptor() == CustomConfigProtocol::descriptor()) {
      return *static_cast<const CustomConfigProtocol *>(server_instance_config_data_.get());
    }

    if (server_instance_config_data_->GetDescriptor()->full_name() == CustomConfigProtocol::descriptor()->full_name()) {
      return *static_cast<const CustomConfigProtocol *>(server_instance_config_data_.get());
    }

    return CustomConfigProtocol::default_instance();
  }

 private:
  void _load_db();
  void _load_db_hosts(PROJECT_NAMESPACE_ID::config::db_group_cfg &out);

  void _load_server_cfg(atfw::atapp::app &app);

 private:
  const PROJECT_NAMESPACE_ID::DConstSettingsType *const_settings_;
  const atframework::ConstSettingsType *atframe_settings_;

  server_instance_config_loader_func server_instance_config_loader_;
  server_instance_config_ptr server_instance_config_data_;

  PROJECT_NAMESPACE_ID::config::logic_section_cfg logic_cfg_;
  std::string readable_app_id_;
};
