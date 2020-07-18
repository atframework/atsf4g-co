#include <sstream>
#include <string>
#include <unordered_set>

#include <std/explicit_declare.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <cli/cmd_option.h>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>

// #include "excel/config_manager.h"

namespace excel {

    namespace detail {
        static const char *skip_space(const char *str) {
            while (str && *str) {
                if (::util::string::is_space(*str)) {
                    ++str;
                    continue;
                }
                break;
            }

            return str;
        }

        template <typename TINT>
        static const char *pick_number(TINT &out, const char *str) {
            out = 0;
            if (NULL == str || !(*str)) {
                return str;
            }

            // negative
            bool is_negative = false;
            while (*str && *str == '-') {
                is_negative = !is_negative;
                ++str;
            }

            if (!(*str)) {
                return str;
            }

            // dec only
            while (str && *str >= '0' && *str <= '9') {
                out *= 10;
                out += *str - '0';
                ++str;
            }

            if (is_negative) {
                out = (~out) + 1;
            }

            return str;
        }

        static void pick_const_data(const std::string &value, google::protobuf::Duration &dur) {
            dur.set_seconds(0);
            dur.set_nanos(0);

            int64_t     tm_val     = 0;
            const char *word_begin = value.c_str();
            word_begin             = skip_space(word_begin);
            word_begin             = pick_number(tm_val, word_begin);
            word_begin             = skip_space(word_begin);

            const char *word_end = value.c_str() + value.size();
            std::string unit;
            if (word_begin && word_end && word_end > word_begin) {
                unit.assign(word_begin, word_end);
                std::transform(unit.begin(), unit.end(), unit.begin(), ::util::string::tolower<char>);
            }

            bool fallback = true;
            do {
                if (unit.empty() || unit == "s" || unit == "sec" || unit == "second" || unit == "seconds") {
                    break;
                }

                if (unit == "ms" || unit == "millisecond" || unit == "milliseconds") {
                    fallback = false;
                    dur.set_seconds(tm_val / 1000);
                    dur.set_nanos((tm_val % 1000) * 1000000);
                    break;
                }

                if (unit == "us" || unit == "microsecond" || unit == "microseconds") {
                    fallback = false;
                    dur.set_seconds(tm_val / 1000000);
                    dur.set_nanos((tm_val % 1000000) * 1000);
                    break;
                }

                if (unit == "ns" || unit == "nanosecond" || unit == "nanoseconds") {
                    fallback = false;
                    dur.set_seconds(tm_val / 1000000000);
                    dur.set_nanos(tm_val % 1000000000);
                    break;
                }

                if (unit == "m" || unit == "minute" || unit == "minutes") {
                    fallback = false;
                    dur.set_seconds(tm_val * 60);
                    break;
                }

                if (unit == "h" || unit == "hour" || unit == "hours") {
                    fallback = false;
                    dur.set_seconds(tm_val * 3600);
                    break;
                }

                if (unit == "d" || unit == "day" || unit == "days") {
                    fallback = false;
                    dur.set_seconds(tm_val * 3600 * 24);
                    break;
                }

                if (unit == "w" || unit == "week" || unit == "weeks") {
                    fallback = false;
                    dur.set_seconds(tm_val * 3600 * 24 * 7);
                    break;
                }

            } while (false);

            // fallback to second
            if (fallback) {
                dur.set_seconds(tm_val);
            }
        }

        static void pick_const_data(const std::string &value, google::protobuf::Timestamp &timepoint) {
            timepoint.set_seconds(0);
            timepoint.set_nanos(0);

            const char *word_begin = value.c_str();
            word_begin             = skip_space(word_begin);

            struct tm t;
            memset(&t, 0, sizeof(t));

            // year
            {
                word_begin = pick_number(t.tm_year, word_begin);
                word_begin = skip_space(word_begin);
                if (*word_begin == '-') {
                    ++word_begin;
                    word_begin = skip_space(word_begin);
                }
                t.tm_year -= 1900; // years since 1900
            }
            // month
            {
                word_begin = pick_number(t.tm_mon, word_begin);
                word_begin = skip_space(word_begin);
                if (*word_begin == '-') {
                    ++word_begin;
                    word_begin = skip_space(word_begin);
                }

                --t.tm_mon; // [0, 11]
            }
            // day
            {
                word_begin = pick_number(t.tm_mday, word_begin);
                word_begin = skip_space(word_begin);
                if (*word_begin == 'T') { // skip T charactor, some format is YYYY-MM-DDThh:mm:ss
                    ++word_begin;
                    word_begin = skip_space(word_begin);
                }
            }

            // tm_hour
            {
                word_begin = pick_number(t.tm_hour, word_begin);
                word_begin = skip_space(word_begin);
                if (*word_begin == ':') { // skip T charactor, some format is YYYY-MM-DDThh:mm:ss
                    ++word_begin;
                    word_begin = skip_space(word_begin);
                }
            }

            // tm_min
            {
                word_begin = pick_number(t.tm_min, word_begin);
                word_begin = skip_space(word_begin);
                if (*word_begin == ':') { // skip T charactor, some format is YYYY-MM-DDThh:mm:ss
                    ++word_begin;
                    word_begin = skip_space(word_begin);
                }
            }

            // tm_sec
            {
                word_begin = pick_number(t.tm_sec, word_begin);
                word_begin = skip_space(word_begin);
            }

            time_t res = mktime(&t);

            if (*word_begin == 'Z') { // UTC timezone
                res -= ::util::time::time_utility::get_sys_zone_offset();
            } else if (*word_begin == '+') {
                res -= ::util::time::time_utility::get_sys_zone_offset();
                time_t offset = 0;
                word_begin    = pick_number(offset, word_begin + 1);
                res -= offset * 60;
                if (*word_begin && ':' == *word_begin) {
                    pick_number(offset, word_begin + 1);
                    res -= offset;
                }
                timepoint.set_seconds(timepoint.seconds() - offset);
            } else if (*word_begin == '-') {
                res -= ::util::time::time_utility::get_sys_zone_offset();
                time_t offset = 0;
                word_begin    = pick_number(offset, word_begin + 1);
                res += offset * 60;
                if (*word_begin && ':' == *word_begin) {
                    pick_number(offset, word_begin + 1);
                    res += offset;
                }
            }

            timepoint.set_seconds(res);
        }

        static bool pick_const_data(::google::protobuf::Message &settings, const ::google::protobuf::FieldDescriptor *fds, const std::string &value) {
            if (NULL == fds) {
                return false;
            }

            switch (fds->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddInt32(&settings, fds, ::util::string::to_int<int32_t>(value.c_str()));
                } else {
                    settings.GetReflection()->SetInt32(&settings, fds, ::util::string::to_int<int32_t>(value.c_str()));
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddInt64(&settings, fds, ::util::string::to_int<int64_t>(value.c_str()));
                } else {
                    settings.GetReflection()->SetInt64(&settings, fds, ::util::string::to_int<int64_t>(value.c_str()));
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddUInt32(&settings, fds, ::util::string::to_int<uint32_t>(value.c_str()));
                } else {
                    settings.GetReflection()->SetUInt32(&settings, fds, ::util::string::to_int<uint32_t>(value.c_str()));
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddUInt64(&settings, fds, ::util::string::to_int<uint64_t>(value.c_str()));
                } else {
                    settings.GetReflection()->SetUInt64(&settings, fds, ::util::string::to_int<uint64_t>(value.c_str()));
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddString(&settings, fds, value);
                } else {
                    settings.GetReflection()->SetString(&settings, fds, value);
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                if (fds->message_type()->full_name() == ::google::protobuf::Duration::descriptor()->full_name()) {
                    if (fds->is_repeated()) {
                        pick_const_data(value, *static_cast< ::google::protobuf::Duration *>(settings.GetReflection()->AddMessage(&settings, fds)));
                    } else {
                        pick_const_data(value, *static_cast< ::google::protobuf::Duration *>(settings.GetReflection()->MutableMessage(&settings, fds)));
                    }
                } else if (fds->message_type()->full_name() == ::google::protobuf::Timestamp::descriptor()->full_name()) {
                    if (fds->is_repeated()) {
                        pick_const_data(value, *static_cast< ::google::protobuf::Timestamp *>(settings.GetReflection()->AddMessage(&settings, fds)));
                    } else {
                        pick_const_data(value, *static_cast< ::google::protobuf::Timestamp *>(settings.GetReflection()->MutableMessage(&settings, fds)));
                    }
                } else {
                    WLOGWARNING("%s in %s with type=%s is not supported now", fds->name().c_str(), settings.GetDescriptor()->full_name().c_str(),
                                fds->type_name());
                }
                return false;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
                double val = 0.0;
                if (!value.empty()) {
                    std::stringstream ss;
                    ss << value;
                    ss >> val;
                }
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddDouble(&settings, fds, val);
                } else {
                    settings.GetReflection()->SetDouble(&settings, fds, val);
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
                float val = 0.0;
                if (!value.empty()) {
                    std::stringstream ss;
                    ss << value;
                    ss >> val;
                }
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddFloat(&settings, fds, val);
                } else {
                    settings.GetReflection()->SetFloat(&settings, fds, val);
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
                bool val = false;
                if (!value.empty() && value != "0" && 0 != UTIL_STRFUNC_STRCASE_CMP(value.c_str(), "no") &&
                    0 != UTIL_STRFUNC_STRCASE_CMP(value.c_str(), "disable") && 0 != UTIL_STRFUNC_STRCASE_CMP(value.c_str(), "false")) {
                    val = true;
                }
                if (fds->is_repeated()) {
                    settings.GetReflection()->AddBool(&settings, fds, val);
                } else {
                    settings.GetReflection()->SetBool(&settings, fds, val);
                }
                return true;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                int                                          val = ::util::string::to_int<int>(value.c_str());
                const google::protobuf::EnumDescriptor *     eds = fds->enum_type();
                const google::protobuf::EnumValueDescriptor *evs = eds->FindValueByNumber(val);
                if (NULL == evs) {
                    WLOGERROR("%s in %s has value %s, but is invalid in it's type %s", fds->name().c_str(), settings.GetDescriptor()->full_name().c_str(),
                              value.c_str(), eds->full_name().c_str());
                    return false;
                } else {
                    if (fds->is_repeated()) {
                        settings.GetReflection()->AddEnum(&settings, fds, evs);
                    } else {
                        settings.GetReflection()->SetEnum(&settings, fds, evs);
                    }
                    return true;
                }
            };
            default: {
                WLOGERROR("%s in %s with type=%s is not supported now", fds->name().c_str(), settings.GetDescriptor()->full_name().c_str(), fds->type_name());
                return false;
            }
            }
        };

        EXPLICIT_UNUSED_ATTR static bool reset_const_value(::google::protobuf::Message &settings, const ::google::protobuf::FieldDescriptor *fds,
                                                           const std::string &value) {
            if (NULL == fds) {
                return false;
            }

            if (!fds->is_repeated()) {
                return pick_const_data(settings, fds, value);
            }

            const char *start      = value.c_str();
            bool        any_failed = false, any_success = false;
            // 分离指令,多个值
            while (*start) {
                std::string splited_val;
                start = util::cli::cmd_option::get_segment(start, splited_val);
                if (pick_const_data(settings, fds, splited_val)) {
                    any_success = true;
                } else {
                    any_failed = true;
                }
            }

            return any_success || !any_failed;
        }
    } // namespace detail

    /**
    void setup_const_config(config_group_t &group) {
        std::unordered_set<std::string> dumped_keys;

        for (auto &kv : group.ResConst.get_all_of_key()) {
            const std::string &key        = std::get<0>(kv.first);
            auto               trimed_key = ::util::string::trim(key.c_str(), key.size());
            if (trimed_key.second == 0 || !trimed_key.first || !*trimed_key.first) {
                continue;
            }

            auto fds = ::tbl::ConstSettings::descriptor()->FindFieldByName(std::string(trimed_key.first, trimed_key.second));
            if (fds == NULL) {
                WLOGWARNING("const config %s=%s, but %s is not found in ConstSettings", kv.second->key().c_str(), kv.second->value().c_str(),
                            kv.second->key().c_str());
                continue;
            }

            if (detail::reset_const_value(group.const_settings, fds, kv.second->value())) {
                if (dumped_keys.end() == dumped_keys.find(fds->name())) {
                    dumped_keys.insert(fds->name());
                } else {
                    WLOGWARNING("const config %s=%s, but %s is set more than one times, we will use the last one", kv.second->key().c_str(),
                                kv.second->value().c_str(), fds->name().c_str());
                }
            }

            if (fds->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING && !fds->is_repeated() && trimed_key.second > 5 &&
                0 == UTIL_STRFUNC_STRNCASE_CMP("i18n_", trimed_key.first, 5)) {
                group.i18n_texts[std::string(trimed_key.first + 5, trimed_key.second - 5)] = kv.second->value();
            }
        }

        for (int i = 0; i < ::tbl::ConstSettings::descriptor()->field_count(); ++i) {
            auto fds = ::tbl::ConstSettings::descriptor()->field(i);
            if (dumped_keys.end() == dumped_keys.find(fds->name())) {
                WLOGWARNING("%s not found in const excel, we will use the previous or default value", fds->full_name().c_str());
            }
        }

        WLOGDEBUG("load %llu const configure items, %llu i18n texts", static_cast<unsigned long long>(dumped_keys.size()),
                  static_cast<unsigned long long>(group.i18n_texts.size()));
    }

    const ::tbl::ConstSettings &get_const_config() {
        auto group = config_manager::me()->get_current_config_group();
        if (!group) {
            return ::tbl::ConstSettings::default_instance();
        }

        return group->const_settings;
    }

    std::string get_i18n_text(const std::string &name, bool *result) {
        auto group = config_manager::me()->get_current_config_group();
        if (!group) {
            if (nullptr != result) {
                *result = false;
            }
            return name;
        }

        auto iter = group->i18n_texts.find(name);
        if (iter == group->i18n_texts.end()) {
            if (nullptr != result) {
                *result = false;
            }
            return name;
        }

        if (nullptr != result) {
            *result = true;
        }
        return iter->second;
    }
    **/

    void parse_timepoint(const std::string &in, google::protobuf::Timestamp &out) { detail::pick_const_data(in, out); }

    void parse_duration(const std::string &in, google::protobuf::Duration &out) { detail::pick_const_data(in, out); }
} // namespace excel
