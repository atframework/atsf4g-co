// Copyright 2021 atframework
// Created by owent on 2016/9/23.
//

#include "config/logic_config.h"

#include <common/string_oprs.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>

#include <sstream>
#include <string>

logic_config::logic_config() : const_settings_(nullptr), atframe_settings_(nullptr) {}
logic_config::~logic_config() {}

int logic_config::init(uint64_t /*server_id*/, const std::string & /*server_name*/) { return 0; }

int logic_config::reload(atapp::app &app) {
  const_settings_ = nullptr;
  atframe_settings_ = nullptr;

  _load_server_cfg(app);
  _load_db();

  return 0;
}

uint64_t logic_config::get_local_server_id() const noexcept {
  auto app = atapp::app::get_last_instance();
  if (nullptr == app) {
    return 0;
  }

  return static_cast<uint32_t>(app->get_app_id());
}

uint32_t logic_config::get_local_zone_id() const noexcept {
  auto app = atapp::app::get_last_instance();
  if (nullptr == app) {
    return 0;
  }

  return static_cast<uint32_t>(app->get_area().zone_id());
}

gsl::string_view logic_config::get_local_server_name() const noexcept {
  auto app = atapp::app::get_last_instance();
  if (nullptr == app) {
    return gsl::string_view();
  }

  return app->get_app_name();
}

gsl::string_view logic_config::get_deployment_environment_name() const noexcept {
  auto app = atapp::app::get_last_instance();
  if (nullptr == app) {
    return gsl::string_view();
  }

  return (*app->mutable_metadata().mutable_labels())["deployment.environment"];
}

void logic_config::_load_db() {
  _load_db_hosts(*server_cfg_.mutable_db()->mutable_cluster());
  _load_db_hosts(*server_cfg_.mutable_db()->mutable_raw());
}

void logic_config::_load_db_hosts(PROJECT_NAMESPACE_ID::config::db_group_cfg &out) {
  for (int i = 0; i < out.host_size(); ++i) {
    const std::string &host = out.host(i);
    out.clear_gateways();

    std::string::size_type fn = host.find_last_of(":");
    if (std::string::npos == fn) {
      PROJECT_NAMESPACE_ID::config::db_group_gateway_cfg *db_gateway = out.add_gateways();
      if (nullptr != db_gateway) {
        db_gateway->set_port(6379);
        db_gateway->set_host(host);
        db_gateway->set_url(LOG_WRAPPER_FWAPI_FORMAT("{}:{}", db_gateway->host(), db_gateway->port()));
      }
    } else {
      // check if it's IP:port-port mode
      std::string::size_type minu_pos = host.find('-', fn + 1);
      if (std::string::npos == minu_pos) {
        // IP:port
        PROJECT_NAMESPACE_ID::config::db_group_gateway_cfg *db_gateway = out.add_gateways();
        if (nullptr != db_gateway) {
          db_gateway->set_port(util::string::to_int<int32_t>(host.c_str() + fn + 1));
          db_gateway->set_host(host.substr(0, fn));
          db_gateway->set_url(LOG_WRAPPER_FWAPI_FORMAT("{}:{}", db_gateway->host(), db_gateway->port()));
        }
      } else {
        // IP:begin_port-end_port
        int32_t begin_port = 0, end_port = 0;
        util::string::str2int(begin_port, &host[fn + 1]);
        util::string::str2int(end_port, &host[minu_pos + 1]);

        for (int32_t port = begin_port; port < end_port; ++port) {
          PROJECT_NAMESPACE_ID::config::db_group_gateway_cfg *db_gateway = out.add_gateways();
          if (nullptr != db_gateway) {
            db_gateway->set_port(port);
            db_gateway->set_host(host.substr(0, fn));
            db_gateway->set_url(LOG_WRAPPER_FWAPI_FORMAT("{}:{}", db_gateway->host(), db_gateway->port()));
          }
        }
      }
    }
  }
}

const PROJECT_NAMESPACE_ID::DConstSettingsType &logic_config::get_const_settings() {
  UTIL_LIKELY_IF(nullptr != const_settings_) { return *const_settings_; }
  auto desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("protocol/pbdesc/com.const.proto");
  if (nullptr == desc) {
    desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("pbdesc/com.const.proto");
  }
  if (nullptr == desc) {
    desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("com.const.proto");
  }
  if (nullptr != desc && desc->options().HasExtension(PROJECT_NAMESPACE_ID::CONST_SETTINGS)) {
    const_settings_ = &desc->options().GetExtension(PROJECT_NAMESPACE_ID::CONST_SETTINGS);
  }

  if (nullptr == const_settings_) {
    return PROJECT_NAMESPACE_ID::DConstSettingsType::default_instance();
  }

  return *const_settings_;
}

const atframework::ConstSettingsType &logic_config::get_atframework_settings() {
  UTIL_LIKELY_IF(nullptr != atframe_settings_) { return *atframe_settings_; }
  auto desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("protocol/pbdesc/atframework.proto");
  if (nullptr == desc) {
    desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("pbdesc/atframework.proto");
  }
  if (nullptr == desc) {
    desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("atframework.proto");
  }
  if (nullptr != desc && desc->options().HasExtension(atframework::CONST_SETTINGS)) {
    atframe_settings_ = &desc->options().GetExtension(atframework::CONST_SETTINGS);
  }

  if (nullptr == atframe_settings_) {
    return atframework::ConstSettingsType::default_instance();
  }

  return *atframe_settings_;
}

void logic_config::_load_server_cfg(atapp::app &app) {
  server_cfg_.Clear();
  app.parse_configures_into(server_cfg_, std::string(), "ATAPP");

  util::time::time_utility::update();
  auto reload_timepoint = server_cfg_.mutable_logic()->mutable_server()->mutable_reload_timepoint();
  reload_timepoint->set_seconds(util::time::time_utility::get_sys_now());
  reload_timepoint->set_nanos(static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000));
}
