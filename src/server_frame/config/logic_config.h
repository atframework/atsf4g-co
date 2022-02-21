// Copyright 2021 atframework
// Created by owent on 2016/9/23.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/svr.protocol.config.pb.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>

#include <config/ini_loader.h>
#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <string>

namespace atapp {
class app;
}

class logic_config : public util::design_pattern::singleton<logic_config> {
 protected:
  logic_config();
  ~logic_config();

 public:
  int init(uint64_t server_id, const std::string &server_name);

  int reload(atapp::app &app);

  uint64_t get_local_server_id() const noexcept;
  uint32_t get_local_zone_id() const noexcept;
  gsl::string_view get_local_server_name() const noexcept;

  gsl::string_view get_deployment_environment_name() const noexcept;

  const PROJECT_NAMESPACE_ID::DConstSettingsType &get_const_settings();
  const atframework::ConstSettingsType &get_atframework_settings();

  inline const PROJECT_NAMESPACE_ID::config::server_cfg &get_server_cfg() const noexcept { return server_cfg_; }
  inline PROJECT_NAMESPACE_ID::config::server_cfg *mutable_server_cfg() noexcept { return &server_cfg_; }
  inline const PROJECT_NAMESPACE_ID::config::db_section_cfg &get_cfg_db() const noexcept { return server_cfg_.db(); }
  inline const PROJECT_NAMESPACE_ID::config::logic_section_cfg &get_logic() const noexcept {
    return server_cfg_.logic();
  }
  inline const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &get_cfg_telemetry() const noexcept {
    return get_logic().telemetry();
  }
  inline const PROJECT_NAMESPACE_ID::config::logic_router_cfg &get_cfg_router() const noexcept {
    return get_logic().router();
  }
  inline const PROJECT_NAMESPACE_ID::config::logic_task_cfg &get_cfg_task() const noexcept {
    return get_logic().task();
  }

  inline const PROJECT_NAMESPACE_ID::config::loginsvr_cfg &get_cfg_loginsvr() const noexcept {
    return get_server_cfg().loginsvr();
  }

 private:
  void _load_db();
  void _load_db_hosts(PROJECT_NAMESPACE_ID::config::db_group_cfg &out);

  void _load_server_cfg(atapp::app &app);

 private:
  const PROJECT_NAMESPACE_ID::DConstSettingsType *const_settings_;
  const atframework::ConstSettingsType *atframe_settings_;

  PROJECT_NAMESPACE_ID::config::server_cfg server_cfg_;
};
