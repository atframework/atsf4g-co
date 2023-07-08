// Copyright 2021 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <string>
#include <unordered_map>

PROJECT_NAMESPACE_BEGIN
namespace config {
class excel_const_config;
}
PROJECT_NAMESPACE_END

namespace google {
namespace protobuf {
class Timestamp;
class Duration;
}  // namespace protobuf
}  // namespace google

namespace excel {

struct config_group_t;

SERVER_FRAME_CONFIG_API void setup_const_config(config_group_t &group);

SERVER_FRAME_CONFIG_API const ::PROJECT_NAMESPACE_ID::config::excel_const_config &get_const_config();

SERVER_FRAME_CONFIG_API void parse_timepoint(const std::string &in, google::protobuf::Timestamp &out);
SERVER_FRAME_CONFIG_API void parse_duration(const std::string &in, google::protobuf::Duration &out);
}  // namespace excel
