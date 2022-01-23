// Copyright 2021 atframework

#include "config/excel_config_const_index.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>
#include <protocol/config/com.const.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <std/explicit_declare.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <cli/cmd_option.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/excel/config_manager.h"

namespace excel {

namespace detail {
static const char* skip_space(const char* str) {
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
static const char* pick_number(TINT& out, const char* str) {
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

static bool protobuf_field_cmp_fn(const ::google::protobuf::FieldDescriptor* l,
                                  const ::google::protobuf::FieldDescriptor* r) {
  int lv = (NULL == l) ? 0 : l->number();
  int rv = (NULL == r) ? 0 : r->number();
  return lv < rv;
}

static const char* pick_const_data_auto_parse(const char* in, google::protobuf::Message& msg) {
  const ::google::protobuf::Descriptor* desc = msg.GetDescriptor();
  const ::google::protobuf::Reflection* reflect = msg.GetReflection();
  if (NULL == desc || NULL == reflect) {
    return in;
  }

  std::vector<const ::google::protobuf::FieldDescriptor*> fds;
  fds.reserve(static_cast<size_t>(desc->field_count()));

  for (int i = 0; i < desc->field_count(); ++i) {
    fds.push_back(desc->field(i));
  }

  std::sort(fds.begin(), fds.end(), protobuf_field_cmp_fn);

  char split_char = 0;
  for (size_t i = 0; i < fds.size() && in && *in; ++i) {
    if (fds[i]->is_repeated()) {
      continue;
    }

    // skip spaces
    in = skip_space(in);

    if (!in || 0 == *in) {
      break;
    }

    switch (fds[i]->cpp_type()) {
      case ::google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
        bool val = false;
        if (in && *in && '0' != *in && 0 != UTIL_STRFUNC_STRNCASE_CMP(in, "no", 2) &&
            0 != UTIL_STRFUNC_STRNCASE_CMP(in, "disable", 7) && 0 != UTIL_STRFUNC_STRNCASE_CMP(in, "false", 5)) {
          val = true;
        }

        while (in && *in && ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') || (*in >= '0' && *in <= '9'))) {
          ++in;
        }

        reflect->SetBool(&msg, fds[i], val);
        break;
      }
      case ::google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
        int val = 0;
        in = pick_number(val, in);
        reflect->SetEnumValue(&msg, fds[i], val);
        break;
      }
      case ::google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
        int32_t val = 0;
        in = pick_number(val, in);
        reflect->SetInt32(&msg, fds[i], val);
        break;
      }
      case ::google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
        uint32_t val = 0;
        in = pick_number(val, in);
        reflect->SetUInt32(&msg, fds[i], val);
        break;
      }
      case ::google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
        int64_t val = 0;
        in = pick_number(val, in);
        reflect->SetInt64(&msg, fds[i], val);
        break;
      }
      case ::google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
        uint64_t val = 0;
        in = pick_number(val, in);
        reflect->SetUInt64(&msg, fds[i], val);
        break;
      }
      default:
        break;
    }

    if (0 == split_char) {
      while (in && *in && ',' != *in && '.' != *in && '|' != *in && ':' != *in && ';' != *in && '_' != *in &&
             '-' != *in && '=' != *in && ' ' != *in && '\t' != *in) {
        ++in;
      }

      if (in) {
        split_char = *in;
        ++in;
      }
    } else {
      while (in && *in && split_char != *in) {
        ++in;
      }

      if (in) {
        ++in;
      }
    }
  }

  return in;
}

static void pick_const_data(const std::string& value, google::protobuf::Duration& dur) {
  dur.set_seconds(0);
  dur.set_nanos(0);

  int64_t tm_val = 0;
  const char* word_begin = value.c_str();
  word_begin = skip_space(word_begin);
  word_begin = pick_number(tm_val, word_begin);
  word_begin = skip_space(word_begin);

  const char* word_end = value.c_str() + value.size();
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

static void pick_const_data(const std::string& value, google::protobuf::Timestamp& timepoint) {
  timepoint.set_seconds(0);
  timepoint.set_nanos(0);

  const char* word_begin = value.c_str();
  word_begin = skip_space(word_begin);

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
    t.tm_year -= 1900;  // years since 1900
  }
  // month
  {
    word_begin = pick_number(t.tm_mon, word_begin);
    word_begin = skip_space(word_begin);
    if (*word_begin == '-') {
      ++word_begin;
      word_begin = skip_space(word_begin);
    }

    --t.tm_mon;  // [0, 11]
  }
  // day
  {
    word_begin = pick_number(t.tm_mday, word_begin);
    word_begin = skip_space(word_begin);
    if (*word_begin == 'T') {  // skip T charactor, some format is YYYY-MM-DDThh:mm:ss
      ++word_begin;
      word_begin = skip_space(word_begin);
    }
  }

  // tm_hour
  {
    word_begin = pick_number(t.tm_hour, word_begin);
    word_begin = skip_space(word_begin);
    if (*word_begin == ':') {  // skip T charactor, some format is YYYY-MM-DDThh:mm:ss
      ++word_begin;
      word_begin = skip_space(word_begin);
    }
  }

  // tm_min
  {
    word_begin = pick_number(t.tm_min, word_begin);
    word_begin = skip_space(word_begin);
    if (*word_begin == ':') {  // skip T charactor, some format is YYYY-MM-DDThh:mm:ss
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

  if (*word_begin == 'Z') {  // UTC timezone
    res -= ::util::time::time_utility::get_sys_zone_offset();
  } else if (*word_begin == '+') {
    res -= ::util::time::time_utility::get_sys_zone_offset();
    time_t offset = 0;
    word_begin = pick_number(offset, word_begin + 1);
    res -= offset * 60;
    if (*word_begin && ':' == *word_begin) {
      pick_number(offset, word_begin + 1);
      res -= offset;
    }
    timepoint.set_seconds(timepoint.seconds() - offset);
  } else if (*word_begin == '-') {
    res -= ::util::time::time_utility::get_sys_zone_offset();
    time_t offset = 0;
    word_begin = pick_number(offset, word_begin + 1);
    res += offset * 60;
    if (*word_begin && ':' == *word_begin) {
      pick_number(offset, word_begin + 1);
      res += offset;
    }
  }

  timepoint.set_seconds(res);
}

static bool pick_const_data(::PROJECT_NAMESPACE_ID::config::excel_const_config& settings,
                            const ::google::protobuf::FieldDescriptor* fds, const std::string& value) {
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
          pick_const_data(
              value, *static_cast<::google::protobuf::Duration*>(settings.GetReflection()->AddMessage(&settings, fds)));
        } else {
          pick_const_data(value, *static_cast<::google::protobuf::Duration*>(
                                     settings.GetReflection()->MutableMessage(&settings, fds)));
        }
        return true;
      } else if (fds->message_type()->full_name() == ::google::protobuf::Timestamp::descriptor()->full_name()) {
        if (fds->is_repeated()) {
          pick_const_data(value, *static_cast<::google::protobuf::Timestamp*>(
                                     settings.GetReflection()->AddMessage(&settings, fds)));
        } else {
          pick_const_data(value, *static_cast<::google::protobuf::Timestamp*>(
                                     settings.GetReflection()->MutableMessage(&settings, fds)));
        }
        return true;
      } else {
        const ::google::protobuf::Descriptor* msg_desc = fds->message_type();
        if (NULL == msg_desc) {
          WLOGWARNING("%s in ConstSettings with type=%s is not supported now", fds->name().c_str(), fds->type_name());
        } else {
          bool auto_parse = true;
          for (int i = 0; auto_parse && i < msg_desc->field_count(); ++i) {
            if (msg_desc->field(i)->is_repeated()) {
              WLOGWARNING("%s in ConstSettings with type=%s is not supported, because it has repeated field",
                          fds->name().c_str(), msg_desc->full_name().c_str());
              auto_parse = false;
            }

            switch (msg_desc->field(i)->cpp_type()) {
              case ::google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
              case ::google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
              case ::google::protobuf::FieldDescriptor::CPPTYPE_INT32:
              case ::google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
              case ::google::protobuf::FieldDescriptor::CPPTYPE_INT64:
              case ::google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                break;
              default:
                WLOGWARNING(
                    "%s in ConstSettings with type=%s is not supported, because it has unsupported field type %s %s",
                    fds->name().c_str(), msg_desc->full_name().c_str(), msg_desc->field(i)->name().c_str(),
                    msg_desc->field(i)->type_name());
                auto_parse = false;
                break;
            }
          }

          if (auto_parse) {
            const char* val = value.c_str();
            if (fds->is_repeated()) {
              while (val && *val) {
                val = pick_const_data_auto_parse(val, *settings.GetReflection()->AddMessage(&settings, fds));
              }
            } else {
              pick_const_data_auto_parse(val, *settings.GetReflection()->MutableMessage(&settings, fds));
            }
            return true;
          }
        }
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
          0 != UTIL_STRFUNC_STRCASE_CMP(value.c_str(), "disable") &&
          0 != UTIL_STRFUNC_STRCASE_CMP(value.c_str(), "false")) {
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
      int val = ::util::string::to_int<int>(value.c_str());
      const google::protobuf::EnumDescriptor* eds = fds->enum_type();
      const google::protobuf::EnumValueDescriptor* evs = eds->FindValueByNumber(val);
      if (NULL == evs) {
        WLOGERROR("%s in ConstSettings has value %s, but is invalid in it's type %s", fds->name().c_str(),
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
      FWLOGERROR("{} in ConstSettings with type={} is not supported now", fds->name(), fds->type_name());
      return false;
    }
  }
}

static bool reset_const_value(::PROJECT_NAMESPACE_ID::config::excel_const_config& settings,
                              const ::google::protobuf::FieldDescriptor* fds, const std::string& value) {
  if (NULL == fds) {
    return false;
  }

  if (!fds->is_repeated()) {
    return pick_const_data(settings, fds, value);
  }

  const char* start = value.c_str();
  bool any_failed = false, any_success = false;
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
}  // namespace detail

void setup_const_config(config_group_t& group) {
  std::unordered_set<std::string> dumped_keys;

  // const data from string configure
  for (auto& kv : group.excel_origin_const_config.get_all_of_key()) {
    auto trimed_key = ::util::string::trim(kv.second->key().c_str(), kv.second->key().size());
    if (trimed_key.second == 0 || !trimed_key.first || !*trimed_key.first) {
      continue;
    }

    auto fds = ::PROJECT_NAMESPACE_ID::config::excel_const_config::descriptor()->FindFieldByName(
        std::string(trimed_key.first, trimed_key.second));
    if (fds == nullptr) {
      FWLOGWARNING("const config {}={}, but {} is not found in ConstSettings", kv.second->key(), kv.second->value(),
                   kv.second->key());
      continue;
    }

    if (detail::reset_const_value(group.const_settings, fds, kv.second->value())) {
      if (dumped_keys.end() == dumped_keys.find(fds->name())) {
        dumped_keys.insert(fds->name());
      } else {
        FWLOGWARNING("const config {}={}, but {} is set more than one times, we will use the last one",
                     kv.second->key(), kv.second->value(), fds->name());
      }
    }
  }

  for (int i = 0; i < ::PROJECT_NAMESPACE_ID::config::excel_const_config::descriptor()->field_count(); ++i) {
    auto fds = ::PROJECT_NAMESPACE_ID::config::excel_const_config::descriptor()->field(i);
    if (dumped_keys.end() == dumped_keys.find(fds->name())) {
      FWLOGWARNING("{} not found in const excel, we will use the previous or default value", fds->full_name());
    }
  }
}

const ::PROJECT_NAMESPACE_ID::config::excel_const_config& get_const_config() {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return ::PROJECT_NAMESPACE_ID::config::excel_const_config::default_instance();
  }

  return group->const_settings;
}

void parse_timepoint(const std::string& in, google::protobuf::Timestamp& out) { detail::pick_const_data(in, out); }

void parse_duration(const std::string& in, google::protobuf::Duration& out) { detail::pick_const_data(in, out); }
}  // namespace excel
