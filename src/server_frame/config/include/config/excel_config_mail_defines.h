// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <config/compile_optimize.h>
#include <gsl/select-gsl.h>

#include <unordered_map>

#include "config/excel_type_trait_setting.h"

#include "config/simple_template/simple_template_core.h"  // IWYU pragma: keep

PROJECT_NAMESPACE_BEGIN
class DMailUserInfo;
class DMailContent;

namespace config {
class ExcelMailTemplate;
}
PROJECT_NAMESPACE_END

using mail_render_extension_type = std::unordered_map<std::string, gsl::string_view>;

struct ATFW_UTIL_SYMBOL_VISIBLE mail_render_data_type {
  const PROJECT_NAMESPACE_ID::DMailUserInfo* sender;
  const PROJECT_NAMESPACE_ID::DMailUserInfo* receiver;
  const PROJECT_NAMESPACE_ID::DMailContent* mail_content;
  const mail_render_extension_type* extensions;

  excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelMailTemplate> template_cfg;
};
