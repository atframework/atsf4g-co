// Copyright 2021 atframework

#pragma once

#include <config/excel_type_trait_setting.h>

#include <string>
#include <unordered_map>

PROJECT_NAMESPACE_BEGIN
namespace config {
class ExcelConstConfig;
}
PROJECT_NAMESPACE_END

namespace google {
namespace protobuf {
class Message;
class FieldDescriptor;
class Timestamp;
class Duration;
}  // namespace protobuf
}  // namespace google

namespace excel {

struct config_group_t;

EXCEL_CONFIG_LOADER_API void setup_const_config(config_group_t &group);

EXCEL_CONFIG_LOADER_API const ::PROJECT_NAMESPACE_ID::config::ExcelConstConfig &get_const_config();

EXCEL_CONFIG_LOADER_API gsl::string_view get_i18n_text(const std::string &name, bool *result = nullptr);
EXCEL_CONFIG_LOADER_API gsl::string_view get_i18n_text(config_group_t &group, const std::string &name,
                                                       bool *result = nullptr);
}  // namespace excel
