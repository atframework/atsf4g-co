#ifndef CONFIG_EXCEL_CONFIG_CONST_INDEX_H
#define CONFIG_EXCEL_CONFIG_CONST_INDEX_H

#pragma once

#include <string>
#include <unordered_map>

/**
namespace tbl {
    class ConstSettings;
}
**/

namespace google {
    namespace protobuf {
        class Timestamp;
        class Duration;
    } // namespace protobuf
} // namespace google

namespace excel {
    // struct config_group_t;
    // void setup_const_config(config_group_t &group);

    // const ::tbl::ConstSettings &get_const_config();
    // std::string get_i18n_text(const std::string &name, bool *result = nullptr);

    void parse_timepoint(const std::string &in, google::protobuf::Timestamp &out);
    void parse_duration(const std::string &in, google::protobuf::Duration &out);
} // namespace excel

#endif