// Copyright 2021 atframework

#pragma once

#include <string>
#include <unordered_map>

namespace hello {
namespace config {
class excel_const_config;
}
}  // namespace hello

namespace google {
namespace protobuf {
class Timestamp;
class Duration;
}  // namespace protobuf
}  // namespace google

namespace excel {
struct config_group_t;
void setup_const_config(config_group_t &group);

const ::hello::config::excel_const_config &get_const_config();

void parse_timepoint(const std::string &in, google::protobuf::Timestamp &out);
void parse_duration(const std::string &in, google::protobuf::Duration &out);
}  // namespace excel
